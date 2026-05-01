// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "EclipsePlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;

/**
 * Player character — third-person, Spring Arm camera (matches the JS prototype's
 * updateThirdPersonCamera). Movement uses default CharacterMovementComponent.
 *
 * Input actions wired via Enhanced Input — see Config/DefaultInput.ini and the
 * IMC_PlayerDefault asset created in editor.
 *
 * Bathroom-slice scope: walk + interact (E) + highlight (TAB hold).
 * Out of slice: dump-clothes (Q), inventory (I), teleport debug (T).
 */
UCLASS()
class ECLIPSE_API AEclipsePlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AEclipsePlayerCharacter();

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// ── Camera ──
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	// ── Input ──
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> HighlightAction;

	// ── Input handlers ──
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnInteract(const FInputActionValue& Value);
	void OnHighlightStart(const FInputActionValue& Value);
	void OnHighlightEnd(const FInputActionValue& Value);
};
