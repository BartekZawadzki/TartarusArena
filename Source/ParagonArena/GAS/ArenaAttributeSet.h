#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "ArenaAttributeSet.generated.h"

#define ARENA_ATTRIBUTE(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/** Hero/minion vitals and stats. Clamping here is the LOCAL enforcement of VR-01. */
UCLASS()
class PARAGONARENA_API UArenaAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly) FGameplayAttributeData Health;
	ARENA_ATTRIBUTE(UArenaAttributeSet, Health)
	UPROPERTY(BlueprintReadOnly) FGameplayAttributeData MaxHealth;
	ARENA_ATTRIBUTE(UArenaAttributeSet, MaxHealth)
	UPROPERTY(BlueprintReadOnly) FGameplayAttributeData Mana;
	ARENA_ATTRIBUTE(UArenaAttributeSet, Mana)
	UPROPERTY(BlueprintReadOnly) FGameplayAttributeData MaxMana;
	ARENA_ATTRIBUTE(UArenaAttributeSet, MaxMana)
	UPROPERTY(BlueprintReadOnly) FGameplayAttributeData Shield;
	ARENA_ATTRIBUTE(UArenaAttributeSet, Shield)
	UPROPERTY(BlueprintReadOnly) FGameplayAttributeData Power;
	ARENA_ATTRIBUTE(UArenaAttributeSet, Power)
	UPROPERTY(BlueprintReadOnly) FGameplayAttributeData Armor;
	ARENA_ATTRIBUTE(UArenaAttributeSet, Armor)
	UPROPERTY(BlueprintReadOnly) FGameplayAttributeData MoveSpeed;
	ARENA_ATTRIBUTE(UArenaAttributeSet, MoveSpeed)

	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
};
