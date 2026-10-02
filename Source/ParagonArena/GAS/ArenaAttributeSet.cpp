#include "GAS/ArenaAttributeSet.h"

void UArenaAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	if (Attribute == GetHealthAttribute()) { NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth()); }
	else if (Attribute == GetManaAttribute()) { NewValue = FMath::Clamp(NewValue, 0.f, GetMaxMana()); }
	else if (Attribute == GetShieldAttribute() || Attribute == GetArmorAttribute() || Attribute == GetPowerAttribute()) { NewValue = FMath::Max(0.f, NewValue); }
	else if (Attribute == GetMoveSpeedAttribute()) { NewValue = FMath::Clamp(NewValue, 0.f, 20.f); }
}
