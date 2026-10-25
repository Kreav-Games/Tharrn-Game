#pragma once

#include "CoreMinimal.h"
#include "Characters/TharrnCharacterBase.h"
#include "TharrnPlayerCharacter.generated.h"

class ATharrnContainer;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UTharrnInventoryComponent;
struct FInputActionValue;

/**
 * The player's own Tuned expedition member. Third-person camera; every gameplay request goes to the server as an RPC
 * and is validated there (reach, line of sight, ownership of the open container, stamina, item availability).
 */
UCLASS()
class THARRN_API ATharrnPlayerCharacter : public ATharrnCharacterBase
{
	GENERATED_BODY()

public:
	ATharrnPlayerCharacter();

	static constexpr int32 HotbarSize = 8;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> FollowCamera;

	/** Index into the inventory; the first HotbarSize slots are the hotbar. */
	UPROPERTY(BlueprintReadOnly, Replicated, SaveGame) int32 ActiveSlot = 0;

	UPROPERTY(BlueprintReadOnly, Replicated) TObjectPtr<AActor> InteractTarget;
	UPROPERTY(BlueprintReadOnly, Replicated) float InteractStartTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Replicated) float InteractDuration = 0.f;
	UPROPERTY(BlueprintReadOnly, Replicated) TObjectPtr<ATharrnContainer> OpenContainer;

	/** Client-side focus from the camera trace, for the prompt. */
	UPROPERTY(Transient) TObjectPtr<AActor> FocusActor;
	FString FocusPrompt;
	FString FocusBlockedReason;

	UPROPERTY(EditAnywhere) float FocusRange = 420.f;
	/** Server check: distance from the player to the target's bounds. */
	UPROPERTY(EditAnywhere) float MaxInteractDistance = 260.f;

	/** Local UI state: the pack panel is open. */
	bool bInventoryOpen = false;
	/** Server-side record of recent notifications (tests read it). */
	TArray<FString> RecentMessages;

	// Owning-client requests.
	UFUNCTION(Server, Reliable) void ServerBeginInteract(AActor* Target);
	UFUNCTION(Server, Reliable) void ServerCancelInteract();
	UFUNCTION(Server, Reliable) void ServerSetActiveSlot(int32 Slot);
	UFUNCTION(Server, Reliable) void ServerUseSlot(int32 Slot);
	UFUNCTION(Server, Reliable) void ServerAttack();
	UFUNCTION(Server, Reliable) void ServerSetBlocking(bool bBlock);
	UFUNCTION(Server, Reliable) void ServerDodge(FVector Direction);
	UFUNCTION(Server, Reliable) void ServerSetSprint(bool bSprint);
	UFUNCTION(Server, Reliable) void ServerTransfer(UTharrnInventoryComponent* From, int32 SlotIndex, int32 Count, UTharrnInventoryComponent* To);
	UFUNCTION(Server, Reliable) void ServerTakeAll();
	UFUNCTION(Server, Reliable) void ServerDropSlot(int32 SlotIndex);
	UFUNCTION(Server, Reliable) void ServerCloseContainer();
	UFUNCTION(Client, Reliable) void ClientNotify(const FString& Message);

	// Server-side implementations; tests call these directly.
	bool BeginInteract(AActor* Target, FString& OutReason);
	void CancelInteract();
	bool UseSlot(int32 Slot, FString& OutMessage);
	bool Attack();
	bool TransferBetween(UTharrnInventoryComponent* From, int32 SlotIndex, int32 Count, UTharrnInventoryComponent* To);
	bool DropSlot(int32 SlotIndex);
	void OpenContainerFor(ATharrnContainer* Container);
	void CloseContainer();
	void Notify(const FString& Message);
	bool IsRecovering() const { return bRecovering; }

	const struct FTharrnItemDef* GetActiveItem(const struct FTharrnItemStack** OutStack = nullptr) const;
	virtual FTharrnMeleeStats GetAttackStats() const override;
	virtual FName GetActiveToolTag() const override;
	virtual void WearActiveTool(float Amount) override;
	virtual FString GetDisplayName() const override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;
	virtual void HandleDeath(AActor* Killer) override;

	void FinishRecovery();
	void CompleteInteract();
	void UpdateFocus();
	bool IsWithinReach(const AActor* Target) const;
	void HandleLandedHit(AActor* Victim, ETharrnHitOutcome Outcome);
	bool IsPanelOpen() const;

	void BuildInput();
	UInputAction* MakeAction(FName Name, int32 ValueType);

	void InputMove(const FInputActionValue& Value);
	void InputLook(const FInputActionValue& Value);
	void InputJump();
	void InputJumpStop();
	void InputSprint();
	void InputSprintStop();
	void InputAttack();
	void InputBlock();
	void InputBlockStop();
	void InputDodge();
	void InputInteract();
	void InputInventory();
	void InputHotbar(const FInputActionValue& Value);
	void InputSave();
	void InputLoad();
	void InputHelp();
	void InputClose();

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> MappingContext;
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UInputAction>> Actions;

	FVector2D LastMoveInput = FVector2D::ZeroVector;
	FTimerHandle RecoveryTimer;
	bool bRecovering = false;
	float InteractEndTime = 0.f;
};
