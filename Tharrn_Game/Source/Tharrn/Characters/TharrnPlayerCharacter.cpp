#include "Characters/TharrnPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Combat/TharrnCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/TharrnData.h"
#include "Core/TharrnStatics.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Game/TharrnHUD.h"
#include "Game/TharrnPlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Inventory/TharrnInventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Survival/TharrnVitalsComponent.h"
#include "TimerManager.h"
#include "World/TharrnContainer.h"
#include "World/TharrnDistrictBuilder.h"
#include "World/TharrnInteractable.h"

ATharrnPlayerCharacter::ATharrnPlayerCharacter()
{
	Faction = ETharrnFaction::Expedition;
	BaseColor = FLinearColor(0.15f, 0.32f, 0.62f);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->TargetArmLength = 380.f;
	CameraBoom->SocketOffset = FVector(0.f, 55.f, 75.f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeSize = 14.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

void ATharrnPlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATharrnPlayerCharacter, ActiveSlot);
	DOREPLIFETIME_CONDITION(ATharrnPlayerCharacter, InteractTarget, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ATharrnPlayerCharacter, InteractStartTime, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ATharrnPlayerCharacter, InteractDuration, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ATharrnPlayerCharacter, OpenContainer, COND_OwnerOnly);
}

void ATharrnPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		const FTharrnSurvivalTuning& Tuning = FTharrnData::Get().GetSurvival();
		Inventory->MaxSlots = Tuning.PlayerSlots;
		Inventory->MaxWeight = Tuning.PlayerCarryWeight;
		Combat->OnLandedHit.AddUObject(this, &ATharrnPlayerCharacter::HandleLandedHit);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Input

UInputAction* ATharrnPlayerCharacter::MakeAction(FName Name, int32 ValueType)
{
	// Prefixed so action objects never collide with component names (Inventory, Combat).
	UInputAction* Action = NewObject<UInputAction>(this, *(TEXT("IA_") + Name.ToString()));
	Action->ValueType = static_cast<EInputActionValueType>(ValueType);
	Actions.Add(Name, Action);
	return Action;
}

void ATharrnPlayerCharacter::BuildInput()
{
	if (MappingContext)
	{
		return;
	}
	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_TharrnPlayer"));
	auto Swizzle = [this]() { return NewObject<UInputModifierSwizzleAxis>(MappingContext); };
	auto Negate = [this]() { return NewObject<UInputModifierNegate>(MappingContext); };

	UInputAction* Move = MakeAction(TEXT("Move"), static_cast<int32>(EInputActionValueType::Axis2D));
	MappingContext->MapKey(Move, EKeys::W).Modifiers.Add(Swizzle());
	{
		FEnhancedActionKeyMapping& Back = MappingContext->MapKey(Move, EKeys::S);
		Back.Modifiers.Add(Swizzle());
		Back.Modifiers.Add(Negate());
	}
	MappingContext->MapKey(Move, EKeys::D);
	MappingContext->MapKey(Move, EKeys::A).Modifiers.Add(Negate());

	UInputAction* Look = MakeAction(TEXT("Look"), static_cast<int32>(EInputActionValueType::Axis2D));
	MappingContext->MapKey(Look, EKeys::Mouse2D);

	const int32 Bool = static_cast<int32>(EInputActionValueType::Boolean);
	MappingContext->MapKey(MakeAction(TEXT("Jump"), Bool), EKeys::SpaceBar);
	MappingContext->MapKey(MakeAction(TEXT("Sprint"), Bool), EKeys::LeftShift);
	MappingContext->MapKey(MakeAction(TEXT("Attack"), Bool), EKeys::LeftMouseButton);
	MappingContext->MapKey(MakeAction(TEXT("Block"), Bool), EKeys::RightMouseButton);
	MappingContext->MapKey(MakeAction(TEXT("Dodge"), Bool), EKeys::LeftControl);
	MappingContext->MapKey(MakeAction(TEXT("Interact"), Bool), EKeys::E);
	UInputAction* Pack = MakeAction(TEXT("Inventory"), Bool);
	MappingContext->MapKey(Pack, EKeys::Tab);
	MappingContext->MapKey(Pack, EKeys::I);
	MappingContext->MapKey(MakeAction(TEXT("Save"), Bool), EKeys::F5);
	MappingContext->MapKey(MakeAction(TEXT("Load"), Bool), EKeys::F9);
	MappingContext->MapKey(MakeAction(TEXT("Help"), Bool), EKeys::F1);
	MappingContext->MapKey(MakeAction(TEXT("Close"), Bool), EKeys::Escape);

	// One hotbar action; each number key scales its value to the slot number.
	UInputAction* Hotbar = MakeAction(TEXT("Hotbar"), static_cast<int32>(EInputActionValueType::Axis1D));
	const FKey Keys[HotbarSize] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight};
	for (int32 Index = 0; Index < HotbarSize; ++Index)
	{
		UInputModifierScalar* Scale = NewObject<UInputModifierScalar>(MappingContext);
		Scale->Scalar = FVector(Index + 1);
		MappingContext->MapKey(Hotbar, Keys[Index]).Modifiers.Add(Scale);
	}
}

void ATharrnPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildInput();
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		return;
	}
	Input->BindAction(Actions[TEXT("Move")], ETriggerEvent::Triggered, this, &ATharrnPlayerCharacter::InputMove);
	Input->BindAction(Actions[TEXT("Move")], ETriggerEvent::Completed, this, &ATharrnPlayerCharacter::InputMove);
	Input->BindAction(Actions[TEXT("Look")], ETriggerEvent::Triggered, this, &ATharrnPlayerCharacter::InputLook);
	Input->BindAction(Actions[TEXT("Jump")], ETriggerEvent::Started, this, &ATharrnPlayerCharacter::InputJump);
	Input->BindAction(Actions[TEXT("Jump")], ETriggerEvent::Completed, this, &ATharrnPlayerCharacter::InputJumpStop);
	Input->BindAction(Actions[TEXT("Sprint")], ETriggerEvent::Started, this, &ATharrnPlayerCharacter::InputSprint);
	Input->BindAction(Actions[TEXT("Sprint")], ETriggerEvent::Completed, this, &ATharrnPlayerCharacter::InputSprintStop);
	Input->BindAction(Actions[TEXT("Attack")], ETriggerEvent::Started, this, &ATharrnPlayerCharacter::InputAttack);
	Input->BindAction(Actions[TEXT("Block")], ETriggerEvent::Started, this, &ATharrnPlayerCharacter::InputBlock);
	Input->BindAction(Actions[TEXT("Block")], ETriggerEvent::Completed, this, &ATharrnPlayerCharacter::InputBlockStop);
	Input->BindAction(Actions[TEXT("Dodge")], ETriggerEvent::Started, this, &ATharrnPlayerCharacter::InputDodge);
	Input->BindAction(Actions[TEXT("Interact")], ETriggerEvent::Started, this, &ATharrnPlayerCharacter::InputInteract);
	Input->BindAction(Actions[TEXT("Inventory")], ETriggerEvent::Started, this, &ATharrnPlayerCharacter::InputInventory);
	Input->BindAction(Actions[TEXT("Hotbar")], ETriggerEvent::Started, this, &ATharrnPlayerCharacter::InputHotbar);
	Input->BindAction(Actions[TEXT("Save")], ETriggerEvent::Started, this, &ATharrnPlayerCharacter::InputSave);
	Input->BindAction(Actions[TEXT("Load")], ETriggerEvent::Started, this, &ATharrnPlayerCharacter::InputLoad);
	Input->BindAction(Actions[TEXT("Help")], ETriggerEvent::Started, this, &ATharrnPlayerCharacter::InputHelp);
	Input->BindAction(Actions[TEXT("Close")], ETriggerEvent::Started, this, &ATharrnPlayerCharacter::InputClose);
}

void ATharrnPlayerCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	BuildInput();
	if (const APlayerController* PC = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(MappingContext, 0);
		}
	}
}

bool ATharrnPlayerCharacter::IsPanelOpen() const
{
	return bInventoryOpen || OpenContainer != nullptr;
}

void ATharrnPlayerCharacter::InputMove(const FInputActionValue& Value)
{
	LastMoveInput = Value.Get<FVector2D>();
	if (!Controller || LastMoveInput.IsNearlyZero())
	{
		return;
	}
	const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), LastMoveInput.Y);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), LastMoveInput.X);
}

void ATharrnPlayerCharacter::InputLook(const FInputActionValue& Value)
{
	if (IsPanelOpen())
	{
		return;
	}
	const FVector2D Look = Value.Get<FVector2D>();
	AddControllerYawInput(Look.X);
	AddControllerPitchInput(-Look.Y);
}

void ATharrnPlayerCharacter::InputJump()
{
	if (!IsDead() && !Combat->bStaggered)
	{
		Jump();
	}
}

void ATharrnPlayerCharacter::InputJumpStop()
{
	StopJumping();
}

void ATharrnPlayerCharacter::InputSprint()
{
	bWantsSprint = true;
	ServerSetSprint(true);
}

void ATharrnPlayerCharacter::InputSprintStop()
{
	bWantsSprint = false;
	ServerSetSprint(false);
}

void ATharrnPlayerCharacter::InputAttack()
{
	if (IsPanelOpen())
	{
		if (ATharrnHUD* HUD = ATharrnHUD::Get(this))
		{
			HUD->HandleClick();
		}
		return;
	}
	ServerAttack();
}

void ATharrnPlayerCharacter::InputBlock()
{
	if (!IsPanelOpen())
	{
		ServerSetBlocking(true);
	}
}

void ATharrnPlayerCharacter::InputBlockStop()
{
	ServerSetBlocking(false);
}

void ATharrnPlayerCharacter::InputDodge()
{
	FVector Direction = -GetActorForwardVector();
	if (Controller && !LastMoveInput.IsNearlyZero())
	{
		const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
		Direction = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * LastMoveInput.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * LastMoveInput.X;
	}
	ServerDodge(Direction);
}

void ATharrnPlayerCharacter::InputInteract()
{
	if (InteractTarget)
	{
		ServerCancelInteract();
	}
	else if (FocusActor)
	{
		ServerBeginInteract(FocusActor);
	}
}

void ATharrnPlayerCharacter::InputInventory()
{
	if (OpenContainer)
	{
		bInventoryOpen = false;
		ServerCloseContainer();
		return;
	}
	bInventoryOpen = !bInventoryOpen;
}

void ATharrnPlayerCharacter::InputHotbar(const FInputActionValue& Value)
{
	const int32 Slot = FMath::RoundToInt(Value.Get<float>()) - 1;
	if (Slot >= 0 && Slot < HotbarSize)
	{
		ServerSetActiveSlot(Slot);
	}
}

void ATharrnPlayerCharacter::InputSave()
{
	if (ATharrnPlayerController* PC = Cast<ATharrnPlayerController>(Controller))
	{
		PC->ServerRequestSave();
	}
}

void ATharrnPlayerCharacter::InputLoad()
{
	if (ATharrnPlayerController* PC = Cast<ATharrnPlayerController>(Controller))
	{
		PC->ServerRequestLoad();
	}
}

void ATharrnPlayerCharacter::InputHelp()
{
	if (ATharrnHUD* HUD = ATharrnHUD::Get(this))
	{
		HUD->bShowHelp = !HUD->bShowHelp;
	}
}

void ATharrnPlayerCharacter::InputClose()
{
	bInventoryOpen = false;
	if (OpenContainer)
	{
		ServerCloseContainer();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Server RPCs

void ATharrnPlayerCharacter::ServerBeginInteract_Implementation(AActor* Target)
{
	FString Reason;
	if (!BeginInteract(Target, Reason) && !Reason.IsEmpty())
	{
		Notify(Reason);
	}
}

void ATharrnPlayerCharacter::ServerCancelInteract_Implementation()
{
	CancelInteract();
}

void ATharrnPlayerCharacter::ServerSetActiveSlot_Implementation(int32 Slot)
{
	if (Slot >= 0 && Slot < HotbarSize)
	{
		ActiveSlot = Slot;
	}
}

void ATharrnPlayerCharacter::ServerUseSlot_Implementation(int32 Slot)
{
	FString Message;
	UseSlot(Slot, Message);
	if (!Message.IsEmpty())
	{
		Notify(Message);
	}
}

void ATharrnPlayerCharacter::ServerAttack_Implementation()
{
	Attack();
}

void ATharrnPlayerCharacter::ServerSetBlocking_Implementation(bool bBlock)
{
	if (!IsDead() && !bRecovering)
	{
		if (bBlock)
		{
			CancelInteract();
		}
		Combat->SetBlocking(bBlock);
	}
}

void ATharrnPlayerCharacter::ServerDodge_Implementation(FVector Direction)
{
	if (!IsDead() && !bRecovering)
	{
		CancelInteract();
		Combat->TryDodge(Direction);
	}
}

void ATharrnPlayerCharacter::ServerSetSprint_Implementation(bool bSprint)
{
	bWantsSprint = bSprint;
}

void ATharrnPlayerCharacter::ServerTransfer_Implementation(UTharrnInventoryComponent* From, int32 SlotIndex, int32 Count, UTharrnInventoryComponent* To)
{
	TransferBetween(From, SlotIndex, Count, To);
}

void ATharrnPlayerCharacter::ServerTakeAll_Implementation()
{
	if (OpenContainer && IsWithinReach(OpenContainer))
	{
		const int32 Before = OpenContainer->Inventory->TotalItemCount();
		const int32 Moved = UTharrnInventoryComponent::TransferAll(OpenContainer->Inventory, Inventory);
		Notify(Moved < Before ? FString::Printf(TEXT("Took %d items; %d did not fit"), Moved, Before - Moved) : FString::Printf(TEXT("Took %d items"), Moved));
	}
}

void ATharrnPlayerCharacter::ServerDropSlot_Implementation(int32 SlotIndex)
{
	DropSlot(SlotIndex);
}

void ATharrnPlayerCharacter::ServerCloseContainer_Implementation()
{
	CloseContainer();
}

void ATharrnPlayerCharacter::ClientNotify_Implementation(const FString& Message)
{
	if (ATharrnHUD* HUD = ATharrnHUD::Get(this))
	{
		HUD->PushMessage(Message);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Server-side behaviour

void ATharrnPlayerCharacter::Notify(const FString& Message)
{
	if (HasAuthority())
	{
		RecentMessages.Add(Message);
		if (RecentMessages.Num() > 40)
		{
			RecentMessages.RemoveAt(0);
		}
	}
	ClientNotify(Message);
}

bool ATharrnPlayerCharacter::IsWithinReach(const AActor* Target) const
{
	if (!IsValid(Target))
	{
		return false;
	}
	FVector Origin;
	FVector Extent;
	// All components, so a depleted node (collision off) still measures where it stands.
	Target->GetActorBounds(false, Origin, Extent);
	const FBox Box(Origin - Extent, Origin + Extent);
	return FMath::Sqrt(Box.ComputeSquaredDistanceToPoint(GetActorLocation())) <= MaxInteractDistance + GetBodyRadius();
}

bool ATharrnPlayerCharacter::BeginInteract(AActor* Target, FString& OutReason)
{
	OutReason.Reset();
	if (!HasAuthority())
	{
		return false;
	}
	if (IsDead() || bRecovering)
	{
		OutReason = TEXT("You are recovering");
		return false;
	}
	ITharrnInteractable* Interactable = Cast<ITharrnInteractable>(Target);
	if (!Interactable)
	{
		OutReason = TEXT("Nothing to use there");
		return false;
	}
	if (!IsWithinReach(Target))
	{
		OutReason = TEXT("Too far away");
		return false;
	}
	OutReason = Interactable->GetBlockedReason(this);
	if (!OutReason.IsEmpty())
	{
		return false;
	}
	CancelInteract();
	Combat->SetBlocking(false);
	const float Duration = Interactable->GetInteractDuration(this);
	if (Duration <= 0.f)
	{
		FString Message;
		const bool bDone = Interactable->Interact(this, Message);
		if (!Message.IsEmpty())
		{
			Notify(Message);
		}
		if (!bDone)
		{
			OutReason = Message;
		}
		return bDone;
	}
	InteractTarget = Target;
	InteractDuration = Duration;
	InteractStartTime = TharrnStatics::ServerTime(GetWorld());
	InteractEndTime = GetWorld()->GetTimeSeconds() + Duration;
	return true;
}

void ATharrnPlayerCharacter::CancelInteract()
{
	InteractTarget = nullptr;
	InteractDuration = 0.f;
}

void ATharrnPlayerCharacter::CompleteInteract()
{
	AActor* Target = InteractTarget;
	CancelInteract();
	ITharrnInteractable* Interactable = Cast<ITharrnInteractable>(Target);
	if (!Interactable)
	{
		return;
	}
	FString Message;
	Interactable->Interact(this, Message);
	if (!Message.IsEmpty())
	{
		Notify(Message);
	}
}

bool ATharrnPlayerCharacter::UseSlot(int32 Slot, FString& OutMessage)
{
	const FTharrnItemStack* Stack = Inventory->GetSlot(Slot);
	const FTharrnItemDef* Def = Stack && !Stack->IsEmpty() ? FTharrnData::Get().FindItem(Stack->ItemId) : nullptr;
	if (!HasAuthority() || !Def || IsDead())
	{
		return false;
	}
	if (Def->Category == ETharrnItemCategory::Food)
	{
		FTharrnItemStack Removed;
		if (Inventory->RemoveFromSlot(Slot, 1, Removed) != 1)
		{
			OutMessage = TEXT("That food is set aside for a task");
			return false;
		}
		Vitals->Consume(*Def);
		OutMessage = FString::Printf(TEXT("Ate %s"), *Def->Name);
		return true;
	}
	if ((Def->Category == ETharrnItemCategory::Tool || Def->Category == ETharrnItemCategory::Weapon) && Slot < HotbarSize)
	{
		ActiveSlot = Slot;
		OutMessage = FString::Printf(TEXT("%s in hand"), *Def->Name);
		return true;
	}
	OutMessage = FString::Printf(TEXT("%s has no use by itself"), *Def->Name);
	return false;
}

bool ATharrnPlayerCharacter::Attack()
{
	if (!HasAuthority() || IsDead() || bRecovering)
	{
		return false;
	}
	CancelInteract();
	if (Controller)
	{
		SetActorRotation(FRotator(0.f, GetControlRotation().Yaw, 0.f));
	}
	return Combat->TryStartAttack(GetAttackStats());
}

bool ATharrnPlayerCharacter::TransferBetween(UTharrnInventoryComponent* From, int32 SlotIndex, int32 Count, UTharrnInventoryComponent* To)
{
	if (!HasAuthority() || !From || !To || IsDead())
	{
		return false;
	}
	// Only between this player's pack and the container they have open and can reach.
	UTharrnInventoryComponent* Open = (OpenContainer && IsWithinReach(OpenContainer)) ? OpenContainer->Inventory.Get() : nullptr;
	const bool bValid = Open && ((From == Inventory && To == Open) || (From == Open && To == Inventory));
	if (!bValid)
	{
		Notify(TEXT("Open the container first"));
		return false;
	}
	const FTharrnItemStack* Stack = From->GetSlot(SlotIndex);
	const FString Name = Stack ? FTharrnData::Get().GetItemName(Stack->ItemId) : FString();
	const int32 Moved = UTharrnInventoryComponent::TransferSlot(From, SlotIndex, Count, To);
	if (Moved <= 0)
	{
		Notify(To == Inventory ? TEXT("No room in your pack") : TEXT("No room in the container"));
		return false;
	}
	Notify(FString::Printf(TEXT("Moved %d %s"), Moved, *Name));
	return true;
}

bool ATharrnPlayerCharacter::DropSlot(int32 SlotIndex)
{
	const FTharrnItemStack* Stack = Inventory->GetSlot(SlotIndex);
	if (!HasAuthority() || !Stack || Stack->IsEmpty())
	{
		return false;
	}
	const FVector Ground = GetActorLocation() + GetActorForwardVector() * 90.f - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 25.f);
	ATharrnContainer* Cache = ATharrnContainer::SpawnCache(GetWorld(), TEXT("dropped_pack"), Ground);
	if (!Cache)
	{
		return false;
	}
	const int32 Moved = UTharrnInventoryComponent::TransferSlot(Inventory, SlotIndex, 0, Cache->Inventory);
	Notify(FString::Printf(TEXT("Dropped %d %s"), Moved, *FTharrnData::Get().GetItemName(Cache->Inventory->GetSlots().Num() > 0 ? Cache->Inventory->GetSlots()[0].ItemId : NAME_None)));
	return Moved > 0;
}

void ATharrnPlayerCharacter::OpenContainerFor(ATharrnContainer* Container)
{
	OpenContainer = Container;
}

void ATharrnPlayerCharacter::CloseContainer()
{
	OpenContainer = nullptr;
}

const FTharrnItemDef* ATharrnPlayerCharacter::GetActiveItem(const FTharrnItemStack** OutStack) const
{
	const FTharrnItemStack* Stack = ActiveSlot < HotbarSize ? Inventory->GetSlot(ActiveSlot) : nullptr;
	if (OutStack)
	{
		*OutStack = Stack;
	}
	return Stack && !Stack->IsEmpty() ? FTharrnData::Get().FindItem(Stack->ItemId) : nullptr;
}

FTharrnMeleeStats ATharrnPlayerCharacter::GetAttackStats() const
{
	const FTharrnItemStack* Stack = nullptr;
	const FTharrnItemDef* Def = GetActiveItem(&Stack);
	if (Def && Def->bHasMelee && Stack && Stack->Durability != 0.f)
	{
		return Def->Melee;
	}
	return Super::GetAttackStats();
}

FName ATharrnPlayerCharacter::GetActiveToolTag() const
{
	const FTharrnItemStack* Stack = nullptr;
	const FTharrnItemDef* Def = GetActiveItem(&Stack);
	return Def && Stack && Stack->Durability != 0.f ? Def->ToolTag : NAME_None;
}

void ATharrnPlayerCharacter::WearActiveTool(float Amount)
{
	const FTharrnItemStack* Stack = nullptr;
	const FTharrnItemDef* Def = GetActiveItem(&Stack);
	if (!HasAuthority() || !Def || !Stack || Stack->Durability < 0.f)
	{
		return;
	}
	const float Before = Stack->Durability;
	Inventory->SetSlotDurability(ActiveSlot, Before - Amount);
	if (Before > 0.f && Before - Amount <= 0.f)
	{
		Notify(FString::Printf(TEXT("%s is broken and needs repair"), *Def->Name));
	}
}

void ATharrnPlayerCharacter::HandleLandedHit(AActor* Victim, ETharrnHitOutcome Outcome)
{
	const FTharrnItemDef* Def = GetActiveItem();
	if (Def && Def->bHasMelee && Outcome != ETharrnHitOutcome::None)
	{
		WearActiveTool(1.f);
	}
}

FString ATharrnPlayerCharacter::GetDisplayName() const
{
	return GetPlayerState() ? GetPlayerState()->GetPlayerName() : TEXT("Expedition member");
}

void ATharrnPlayerCharacter::HandleDeath(AActor* Killer)
{
	Super::HandleDeath(Killer);
	CancelInteract();
	CloseContainer();
	bRecovering = true;
	GetCharacterMovement()->DisableMovement();

	// Injury-and-recovery: the pack stays where the player fell, as a cache they can come back for.
	if (!Inventory->IsEmpty())
	{
		const FVector Ground = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 25.f);
		if (ATharrnContainer* Cache = ATharrnContainer::SpawnCache(GetWorld(), TEXT("recovery_cache"), Ground, FString::Printf(TEXT("%s's pack"), *GetDisplayName())))
		{
			UTharrnInventoryComponent::TransferAll(Inventory, Cache->Inventory);
		}
	}
	Notify(TEXT("You are badly hurt. Your pack lies where you fell."));
	GetWorldTimerManager().SetTimer(RecoveryTimer, this, &ATharrnPlayerCharacter::FinishRecovery, FTharrnData::Get().GetSurvival().RecoverySeconds, false);
}

void ATharrnPlayerCharacter::FinishRecovery()
{
	const FTharrnSurvivalTuning& Tuning = FTharrnData::Get().GetSurvival();
	FVector Point = GetActorLocation();
	if (const ATharrnDistrictBuilder* Builder = ATharrnDistrictBuilder::Find(GetWorld()))
	{
		Point = Builder->GetRecoveryPoint();
	}
	TeleportTo(Point, GetActorRotation(), false, true);
	Vitals->Recover(Tuning.RecoveryHealthFraction, Tuning.RecoveryMinFood, Tuning.RecoveryMinWater);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	bRecovering = false;
	Notify(TEXT("You come round at camp, patched up."));
}

// ---------------------------------------------------------------------------------------------------------------------
// Tick

void ATharrnPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const bool bCombatFacing = Combat->bBlocking || Combat->IsAttacking();
	GetCharacterMovement()->bOrientRotationToMovement = !bCombatFacing;
	GetCharacterMovement()->bUseControllerDesiredRotation = bCombatFacing;

	if (IsLocallyControlled())
	{
		UpdateFocus();
	}
	if (!HasAuthority())
	{
		return;
	}
	if (bWantsSprint && GetVelocity().Size2D() > 50.f && !Combat->bBlocking)
	{
		Vitals->DrainStamina(FTharrnData::Get().GetSurvival().SprintStaminaPerSecond * DeltaSeconds);
		if (Vitals->Stamina <= 1.f)
		{
			bWantsSprint = false;
		}
	}
	if (InteractTarget)
	{
		if (!IsWithinReach(InteractTarget) || IsDead() || Combat->bStaggered || Combat->IsAttacking())
		{
			CancelInteract();
			Notify(TEXT("Interrupted"));
		}
		else if (GetWorld()->GetTimeSeconds() >= InteractEndTime)
		{
			CompleteInteract();
		}
	}
	if (OpenContainer && !IsWithinReach(OpenContainer))
	{
		CloseContainer();
	}
}

void ATharrnPlayerCharacter::UpdateFocus()
{
	FocusActor = nullptr;
	FocusPrompt.Reset();
	FocusBlockedReason.Reset();
	const APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC || !FollowCamera)
	{
		return;
	}
	const FVector Start = FollowCamera->GetComponentLocation();
	const FVector End = Start + FollowCamera->GetForwardVector() * (CameraBoom->TargetArmLength + FocusRange);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TharrnFocus), false, this);
	TArray<FHitResult> Hits;
	GetWorld()->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(20.f), Params);
	for (const FHitResult& Hit : Hits)
	{
		AActor* Actor = Hit.GetActor();
		ITharrnInteractable* Interactable = Cast<ITharrnInteractable>(Actor);
		if (Interactable && IsWithinReach(Actor))
		{
			FocusActor = Actor;
			FocusPrompt = Interactable->GetInteractPrompt(this);
			FocusBlockedReason = Interactable->GetBlockedReason(this);
			return;
		}
		if (Hit.bBlockingHit && !Interactable)
		{
			return;
		}
	}
}
