#include "Game/TharrnHUD.h"

#include "Characters/TharrnCreature.h"
#include "Characters/TharrnPlayerCharacter.h"
#include "Combat/TharrnCombatComponent.h"
#include "Core/TharrnData.h"
#include "Core/TharrnStatics.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/TharrnInventoryComponent.h"
#include "Survival/TharrnVitalsComponent.h"
#include "World/TharrnContainer.h"

namespace
{
	const FLinearColor PanelColor(0.02f, 0.025f, 0.03f, 0.82f);
	const FLinearColor TextColor(0.92f, 0.9f, 0.85f);
	const FLinearColor DimText(0.6f, 0.6f, 0.58f);
	const FLinearColor Accent(1.f, 0.72f, 0.35f);
	const FLinearColor Warning(1.f, 0.35f, 0.25f);

	FString OutcomeText(ETharrnHitOutcome Outcome, float Damage)
	{
		switch (Outcome)
		{
		case ETharrnHitOutcome::Blocked: return FString::Printf(TEXT("BLOCKED -%.0f"), Damage);
		case ETharrnHitOutcome::GuardBroken: return FString::Printf(TEXT("GUARD BROKEN -%.0f"), Damage);
		case ETharrnHitOutcome::Parried: return TEXT("PARRIED");
		case ETharrnHitOutcome::Dodged: return TEXT("DODGED");
		case ETharrnHitOutcome::Staggered: return FString::Printf(TEXT("STAGGER -%.0f"), Damage);
		default: return FString::Printf(TEXT("-%.0f"), Damage);
		}
	}
}

ATharrnHUD* ATharrnHUD::Get(const AActor* Context)
{
	const APawn* Pawn = Cast<APawn>(Context);
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : Cast<APlayerController>(Context);
	return PC ? Cast<ATharrnHUD>(PC->GetHUD()) : nullptr;
}

void ATharrnHUD::PushMessage(const FString& Message)
{
	Messages.Add({Message, GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f});
	if (Messages.Num() > 7)
	{
		Messages.RemoveAt(0);
	}
}

float ATharrnHUD::S() const
{
	return Canvas ? FMath::Max(0.6f, Canvas->ClipY / 1080.f) : 1.f;
}

void ATharrnHUD::Text(const FString& Value, float X, float Y, const FLinearColor& Color, float Scale, bool bLarge)
{
	UFont* Font = bLarge ? GEngine->GetLargeFont() : GEngine->GetMediumFont();
	DrawText(Value, Color, X, Y, Font, Scale * S());
}

void ATharrnHUD::Bar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Color, const FString& Label)
{
	DrawRect(PanelColor, X, Y, W, H);
	DrawRect(Color, X + 2.f, Y + 2.f, FMath::Clamp(Fraction, 0.f, 1.f) * (W - 4.f), H - 4.f);
	if (!Label.IsEmpty())
	{
		Text(Label, X + 6.f, Y + H * 0.5f - 9.f * S(), TextColor, 0.85f);
	}
}

void ATharrnHUD::Button(const FString& Label, float X, float Y, float W, float H, const FLinearColor& Color, TFunction<void()> Action)
{
	DrawRect(Color, X, Y, W, H);
	Text(Label, X + 6.f * S(), Y + 3.f * S(), TextColor, 0.8f);
	Regions.Add({FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)), MoveTemp(Action)});
}

void ATharrnHUD::HandleClick()
{
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!PlayerOwner || !PlayerOwner->GetMousePosition(MouseX, MouseY))
	{
		return;
	}
	for (const FRegion& Region : Regions)
	{
		if (Region.Box.IsInside(FVector2D(MouseX, MouseY)) && Region.Action)
		{
			Region.Action();
			return;
		}
	}
}

FString ATharrnHUD::SlotText(const UTharrnInventoryComponent* Inventory, int32 Index) const
{
	const FTharrnItemStack* Stack = Inventory ? Inventory->GetSlot(Index) : nullptr;
	if (!Stack || Stack->IsEmpty())
	{
		return TEXT("-");
	}
	const FTharrnItemDef* Def = FTharrnData::Get().FindItem(Stack->ItemId);
	FString Name = Def ? Def->Name : Stack->ItemId.ToString();
	if (Def && Def->UsesDurability())
	{
		return FString::Printf(TEXT("%s  [%.0f/%.0f]"), *Name, Stack->Durability, Def->MaxDurability);
	}
	return FString::Printf(TEXT("%s  x%d"), *Name, Stack->Count);
}

void ATharrnHUD::DrawHUD()
{
	Super::DrawHUD();
	Regions.Reset();
	ATharrnPlayerCharacter* Player = PlayerOwner ? Cast<ATharrnPlayerCharacter>(PlayerOwner->GetPawn()) : nullptr;
	if (!Canvas || !Player)
	{
		return;
	}
	DrawWorldMarkers(Player);
	DrawVitals(Player);
	DrawHotbar(Player);
	DrawPrompt(Player);
	DrawMessages();

	const float Sc = S();
	if (Player->OpenContainer)
	{
		DrawPackPanel(Player, Canvas->ClipX * 0.5f - 470.f * Sc, 120.f * Sc);
		DrawContainerPanel(Player, Canvas->ClipX * 0.5f + 20.f * Sc, 120.f * Sc);
	}
	else if (Player->bInventoryOpen)
	{
		DrawPackPanel(Player, Canvas->ClipX * 0.5f - 225.f * Sc, 120.f * Sc);
	}
	if (Player->IsDead() || Player->IsRecovering())
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
		Text(TEXT("Badly hurt - recovering..."), Canvas->ClipX * 0.5f - 160.f * Sc, Canvas->ClipY * 0.45f, Warning, 1.2f, true);
	}
	if (bShowHelp)
	{
		DrawHelp();
	}
	else
	{
		Text(TEXT("F1  controls"), Canvas->ClipX - 140.f * Sc, 14.f * Sc, DimText, 0.85f);
	}
	if (!Caption.IsEmpty())
	{
		DrawRect(PanelColor, 0.f, Canvas->ClipY - 34.f * Sc, Canvas->ClipX, 34.f * Sc);
		Text(Caption, 16.f * Sc, Canvas->ClipY - 30.f * Sc, Accent, 1.f);
	}
}

void ATharrnHUD::DrawVitals(ATharrnPlayerCharacter* Player)
{
	const UTharrnVitalsComponent* V = Player->Vitals;
	const float Sc = S();
	const float X = 24.f * Sc;
	float Y = Canvas->ClipY - 230.f * Sc;
	const float W = 300.f * Sc;
	const float H = 24.f * Sc;
	Bar(X, Y, W, H, V->Health / V->MaxHealth, FLinearColor(0.7f, 0.12f, 0.1f), FString::Printf(TEXT("Health %.0f"), V->Health));
	Y += H + 6.f * Sc;
	Bar(X, Y, W, H, V->Stamina / V->MaxStamina, FLinearColor(0.35f, 0.62f, 0.22f), FString::Printf(TEXT("Stamina %.0f"), V->Stamina));
	Y += H + 6.f * Sc;
	Bar(X, Y, W * 0.49f, H, V->Food / V->MaxFood, FLinearColor(0.75f, 0.45f, 0.12f), FString::Printf(TEXT("Food %.0f"), V->Food));
	Bar(X + W * 0.51f, Y, W * 0.49f, H, V->Water / V->MaxWater, FLinearColor(0.12f, 0.4f, 0.75f), FString::Printf(TEXT("Water %.0f"), V->Water));
	Y += H + 8.f * Sc;

	const UTharrnInventoryComponent* Inv = Player->Inventory;
	const float Ratio = Inv->GetWeightRatio();
	FString Load = FString::Printf(TEXT("Load %.1f / %.0f"), Inv->GetTotalWeight(), Inv->MaxWeight);
	if (Ratio > 1.f)
	{
		Load += TEXT("  OVERBURDENED");
	}
	Text(Load, X, Y, Ratio > 1.f ? Warning : DimText, 0.9f);
	Y += 22.f * Sc;
	FString Effects;
	for (const FTharrnActiveEffect& Effect : V->Effects)
	{
		const FTharrnEffectDef* Def = FTharrnData::Get().FindEffect(Effect.EffectId);
		Effects += FString::Printf(TEXT("%s %d:%02d   "), Def ? *Def->Name : *Effect.EffectId.ToString(), FMath::FloorToInt(Effect.Remaining / 60.f), FMath::FloorToInt(Effect.Remaining) % 60);
	}
	if (V->Food < 15.f || V->Water < 15.f)
	{
		Effects += V->Food <= 0.f || V->Water <= 0.f ? TEXT("STARVING / PARCHED - losing health") : TEXT("Hungry / thirsty");
	}
	Text(Effects, X, Y, Accent, 0.85f);

	const UTharrnCombatComponent* C = Player->Combat;
	FString Stance = C->bStaggered ? TEXT("STAGGERED") : C->bBlocking ? TEXT("Blocking") : C->bDodging ? TEXT("Dodging") : FString();
	if (!Stance.IsEmpty())
	{
		Text(Stance, X + W + 14.f * Sc, Canvas->ClipY - 230.f * Sc, C->bStaggered ? Warning : Accent, 0.95f);
	}
}

void ATharrnHUD::DrawHotbar(ATharrnPlayerCharacter* Player)
{
	const float Sc = S();
	const float Box = 92.f * Sc;
	const float Gap = 6.f * Sc;
	const int32 Count = ATharrnPlayerCharacter::HotbarSize;
	const float Total = Count * Box + (Count - 1) * Gap;
	float X = Canvas->ClipX * 0.5f - Total * 0.5f;
	const float Y = Canvas->ClipY - Box - 22.f * Sc;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const bool bActive = Player->ActiveSlot == Index;
		DrawRect(bActive ? FLinearColor(0.45f, 0.3f, 0.1f, 0.9f) : PanelColor, X, Y, Box, Box);
		Text(FString::FromInt(Index + 1), X + 4.f * Sc, Y + 2.f * Sc, DimText, 0.75f);
		const FTharrnItemStack* Stack = Player->Inventory->GetSlot(Index);
		if (Stack && !Stack->IsEmpty())
		{
			const FTharrnItemDef* Def = FTharrnData::Get().FindItem(Stack->ItemId);
			const FLinearColor ItemColor = Def ? Def->Color : FLinearColor::Gray;
			DrawRect(ItemColor, X + Box * 0.3f, Y + Box * 0.22f, Box * 0.4f, Box * 0.3f);
			const FString Name = Def ? Def->Name.Left(11) : Stack->ItemId.ToString();
			Text(Name, X + 4.f * Sc, Y + Box * 0.56f, TextColor, 0.72f);
			if (Def && Def->UsesDurability())
			{
				const float Frac = Stack->Durability / Def->MaxDurability;
				DrawRect(Frac <= 0.f ? Warning : FLinearColor(0.4f, 0.75f, 0.3f), X + 4.f * Sc, Y + Box - 10.f * Sc, (Box - 8.f * Sc) * FMath::Max(0.03f, Frac), 5.f * Sc);
			}
			else
			{
				Text(FString::Printf(TEXT("x%d"), Stack->Count), X + 4.f * Sc, Y + Box * 0.76f, Accent, 0.72f);
			}
		}
		X += Box + Gap;
	}
}

void ATharrnHUD::DrawPrompt(ATharrnPlayerCharacter* Player)
{
	const float Sc = S();
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.6f), CX - 2.f, CY - 2.f, 4.f, 4.f);
	if (Player->InteractTarget && Player->InteractDuration > 0.f)
	{
		const float Elapsed = TharrnStatics::ServerTime(GetWorld()) - Player->InteractStartTime;
		Bar(CX - 120.f * Sc, CY + 60.f * Sc, 240.f * Sc, 16.f * Sc, Elapsed / Player->InteractDuration, Accent, FString());
		Text(TEXT("[E] cancel"), CX - 40.f * Sc, CY + 80.f * Sc, DimText, 0.8f);
		return;
	}
	if (Player->FocusActor)
	{
		const bool bBlocked = !Player->FocusBlockedReason.IsEmpty();
		const FString Line = bBlocked ? FString::Printf(TEXT("%s  -  %s"), *Player->FocusPrompt, *Player->FocusBlockedReason) : FString::Printf(TEXT("[E]  %s"), *Player->FocusPrompt);
		float W = 0.f;
		float H = 0.f;
		GetTextSize(Line, W, H, GEngine->GetMediumFont(), Sc);
		DrawRect(PanelColor, CX - W * 0.5f - 10.f, CY + 52.f * Sc, W + 20.f, H + 8.f);
		Text(Line, CX - W * 0.5f, CY + 56.f * Sc, bBlocked ? Warning : TextColor);
	}
}

void ATharrnHUD::DrawPackPanel(ATharrnPlayerCharacter* Player, float X, float Y)
{
	const float Sc = S();
	const float W = 450.f * Sc;
	const float Row = 24.f * Sc;
	UTharrnInventoryComponent* Inv = Player->Inventory;
	const int32 Count = Inv->MaxSlots;
	DrawRect(PanelColor, X, Y, W, (Count + 3) * Row);
	const bool bContainer = Player->OpenContainer != nullptr;
	Text(bContainer ? TEXT("PACK  -  click to store") : TEXT("PACK  -  click: eat / hold    [drop]"), X + 10.f * Sc, Y + 4.f * Sc, Accent, 0.9f);
	TWeakObjectPtr<ATharrnPlayerCharacter> WeakPlayer(Player);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float RowY = Y + (Index + 1.3f) * Row;
		const bool bHotbar = Index < ATharrnPlayerCharacter::HotbarSize;
		const FString Label = FString::Printf(TEXT("%s%2d  %s"), bHotbar ? TEXT("*") : TEXT(" "), Index + 1, *SlotText(Inv, Index));
		const FTharrnItemStack* Stack = Inv->GetSlot(Index);
		const bool bFilled = Stack && !Stack->IsEmpty();
		Button(Label, X + 8.f * Sc, RowY, W - 80.f * Sc, Row - 3.f * Sc, bFilled ? FLinearColor(0.1f, 0.11f, 0.12f, 0.9f) : FLinearColor(0.05f, 0.05f, 0.05f, 0.6f),
			[WeakPlayer, Index]()
			{
				ATharrnPlayerCharacter* P = WeakPlayer.Get();
				if (!P)
				{
					return;
				}
				if (P->OpenContainer)
				{
					P->ServerTransfer(P->Inventory, Index, 0, P->OpenContainer->Inventory);
				}
				else
				{
					P->ServerUseSlot(Index);
				}
			});
		if (bFilled && !bContainer)
		{
			Button(TEXT("drop"), X + W - 66.f * Sc, RowY, 58.f * Sc, Row - 3.f * Sc, FLinearColor(0.3f, 0.08f, 0.06f, 0.9f),
				[WeakPlayer, Index]() { if (WeakPlayer.IsValid()) { WeakPlayer->ServerDropSlot(Index); } });
		}
	}
	Text(FString::Printf(TEXT("Slots %d/%d   Load %.1f/%.0f"), Inv->GetUsedSlots(), Inv->MaxSlots, Inv->GetTotalWeight(), Inv->MaxWeight),
		X + 10.f * Sc, Y + (Count + 1.6f) * Row, DimText, 0.85f);
}

void ATharrnHUD::DrawContainerPanel(ATharrnPlayerCharacter* Player, float X, float Y)
{
	const float Sc = S();
	const float W = 450.f * Sc;
	const float Row = 24.f * Sc;
	ATharrnContainer* Container = Player->OpenContainer;
	UTharrnInventoryComponent* Inv = Container->Inventory;
	const int32 Count = Inv->MaxSlots;
	DrawRect(PanelColor, X, Y, W, (Count + 3) * Row);
	Text(FString::Printf(TEXT("%s  -  click to take"), *Container->DisplayName.ToUpper()), X + 10.f * Sc, Y + 4.f * Sc, Accent, 0.9f);
	TWeakObjectPtr<ATharrnPlayerCharacter> WeakPlayer(Player);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float RowY = Y + (Index + 1.3f) * Row;
		const FTharrnItemStack* Stack = Inv->GetSlot(Index);
		const bool bFilled = Stack && !Stack->IsEmpty();
		Button(FString::Printf(TEXT("%2d  %s"), Index + 1, *SlotText(Inv, Index)), X + 8.f * Sc, RowY, W - 16.f * Sc, Row - 3.f * Sc,
			bFilled ? FLinearColor(0.1f, 0.11f, 0.12f, 0.9f) : FLinearColor(0.05f, 0.05f, 0.05f, 0.6f),
			[WeakPlayer, Index]()
			{
				ATharrnPlayerCharacter* P = WeakPlayer.Get();
				if (P && P->OpenContainer)
				{
					P->ServerTransfer(P->OpenContainer->Inventory, Index, 0, P->Inventory);
				}
			});
	}
	Button(TEXT("Take all"), X + 8.f * Sc, Y + (Count + 1.5f) * Row, 120.f * Sc, Row, FLinearColor(0.2f, 0.3f, 0.12f, 0.95f),
		[WeakPlayer]() { if (WeakPlayer.IsValid()) { WeakPlayer->ServerTakeAll(); } });
	Button(TEXT("Close"), X + W - 90.f * Sc, Y + (Count + 1.5f) * Row, 82.f * Sc, Row, FLinearColor(0.25f, 0.1f, 0.08f, 0.95f),
		[WeakPlayer]() { if (WeakPlayer.IsValid()) { WeakPlayer->ServerCloseContainer(); } });
}

void ATharrnHUD::DrawWorldMarkers(ATharrnPlayerCharacter* Player)
{
	const float Sc = S();
	const float Now = GetWorld()->GetTimeSeconds();
	for (TActorIterator<ATharrnCharacterBase> It(GetWorld()); It; ++It)
	{
		ATharrnCharacterBase* Character = *It;
		const float Distance = FVector::Dist(Character->GetActorLocation(), Player->GetActorLocation());
		if (Distance > 3500.f)
		{
			continue;
		}
		const FVector Head = Character->GetActorLocation() + FVector(0.f, 0.f, 120.f);
		const FVector Screen = Project(Head);
		if (Screen.Z <= 0.f)
		{
			continue;
		}
		if (Character != Player && !Character->IsDead())
		{
			const float W = 90.f * Sc;
			Bar(Screen.X - W * 0.5f, Screen.Y, W, 8.f * Sc, Character->Vitals->GetHealthFraction(), FLinearColor(0.75f, 0.15f, 0.1f), FString());
			Text(Character->GetDisplayName(), Screen.X - W * 0.5f, Screen.Y - 18.f * Sc, DimText, 0.75f);
			if (Character->Combat->Phase == ETharrnAttackPhase::Windup)
			{
				Text(TEXT("!"), Screen.X - 6.f * Sc, Screen.Y - 50.f * Sc, Warning, 2.f, true);
			}
		}
		if (Now - Character->LastEventTime < 1.f && Character->LastEventOutcome != ETharrnHitOutcome::None)
		{
			const float Rise = (Now - Character->LastEventTime) * 40.f * Sc;
			const bool bGood = Character == Player ? Character->LastEventOutcome == ETharrnHitOutcome::Blocked || Character->LastEventOutcome == ETharrnHitOutcome::Parried || Character->LastEventOutcome == ETharrnHitOutcome::Dodged
				: Character->LastEventOutcome != ETharrnHitOutcome::Parried && Character->LastEventOutcome != ETharrnHitOutcome::Dodged;
			Text(OutcomeText(Character->LastEventOutcome, Character->LastEventDamage), Screen.X + 30.f * Sc, Screen.Y - 30.f * Sc - Rise, bGood ? Accent : Warning, 1.f);
		}
	}
}

void ATharrnHUD::DrawMessages()
{
	const float Sc = S();
	const float Now = GetWorld()->GetTimeSeconds();
	float Y = 18.f * Sc;
	for (const FMessage& Message : Messages)
	{
		const float Age = Now - Message.Time;
		if (Age > 7.f)
		{
			continue;
		}
		FLinearColor Color = TextColor;
		Color.A = FMath::Clamp(7.f - Age, 0.f, 1.f);
		Text(Message.Text, 24.f * Sc, Y, Color, 0.95f);
		Y += 24.f * Sc;
	}
}

void ATharrnHUD::DrawHelp()
{
	const float Sc = S();
	const float X = Canvas->ClipX - 420.f * Sc;
	const float Y = 14.f * Sc;
	const TCHAR* Lines[] = {
		TEXT("WASD move    Mouse look    Space jump"),
		TEXT("Shift sprint (stamina)    Ctrl dodge"),
		TEXT("LMB attack    RMB block (raise it as a hit lands to parry)"),
		TEXT("E use / gather / open    1-8 hold item"),
		TEXT("Tab or I pack (click to eat, hold or drop)"),
		TEXT("F5 save    F9 load (host)    F1 hide"),
	};
	DrawRect(PanelColor, X - 10.f * Sc, Y, 420.f * Sc, (UE_ARRAY_COUNT(Lines) + 1) * 22.f * Sc);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Lines); ++Index)
	{
		Text(Lines[Index], X, Y + (Index + 0.4f) * 22.f * Sc, TextColor, 0.8f);
	}
}
