#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMin.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionEyeAdaptationInverse.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVertexInterpolator.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialParameterCollection.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"

CAMPUSOPSEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogCampusBootstrap, Log, All);

namespace CampusEditor
{
	inline bool SaveAsset(UObject* Asset, bool bMap = false)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), bMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.Error = GWarn;
		const bool bOk = UPackage::SavePackage(Package, Asset, *Filename, Args);
		UE_LOG(LogCampusBootstrap, Display, TEXT("%s %s"), bOk ? TEXT("Saved") : TEXT("FAILED to save"), *Filename);
		return bOk;
	}

	/** Small helper for authoring material graphs from code. */
	struct FMat
	{
		UMaterial* M = nullptr;
		int32 Y = -800;

		UMaterialExpression* Make(UClass* Class)
		{
			Y += 90;
			return UMaterialEditingLibrary::CreateMaterialExpression(M, Class, -500, Y);
		}
		template <typename T>
		T* Make() { return Cast<T>(Make(T::StaticClass())); }

		void Link(UMaterialExpression* From, UMaterialExpression* To, const TCHAR* Input = TEXT(""), const TCHAR* Output = TEXT(""))
		{
			if (!UMaterialEditingLibrary::ConnectMaterialExpressions(From, Output, To, Input))
			{
				UE_LOG(LogCampusBootstrap, Warning, TEXT("Failed to connect %s.%s -> %s.%s"), *GetNameSafe(From), Output, *GetNameSafe(To), Input);
			}
		}
		/** Value * Scale divided by the current exposure: renders at the same brightness however the eye has adapted. */
		UMaterialExpression* ExposureCompensated(UMaterialExpression* Value, float Scale, const TCHAR* Output = TEXT(""))
		{
			UMaterialExpressionMultiply* E = Make<UMaterialExpressionMultiply>();
			Link(Value, E, TEXT("A"), Output);
			Link(Make<UMaterialExpressionEyeAdaptationInverse>(), E, TEXT("B"));
			return MulC(E, Scale);
		}
		void Out(UMaterialExpression* From, EMaterialProperty Property, const TCHAR* Output = TEXT(""))
		{
			if (!UMaterialEditingLibrary::ConnectMaterialProperty(From, Output, Property))
			{
				UE_LOG(LogCampusBootstrap, Warning, TEXT("Failed to connect %s.%s to material property %d"), *GetNameSafe(From), Output, (int32)Property);
			}
		}
		UMaterialExpression* Vec(const TCHAR* Name, const FLinearColor& Default)
		{
			UMaterialExpressionVectorParameter* E = Make<UMaterialExpressionVectorParameter>();
			E->ParameterName = Name;
			E->DefaultValue = Default;
			return E;
		}
		UMaterialExpression* Scalar(const TCHAR* Name, float Default)
		{
			UMaterialExpressionScalarParameter* E = Make<UMaterialExpressionScalarParameter>();
			E->ParameterName = Name;
			E->DefaultValue = Default;
			return E;
		}
		UMaterialExpression* Const(float V)
		{
			UMaterialExpressionConstant* E = Make<UMaterialExpressionConstant>();
			E->R = V;
			return E;
		}
		UMaterialExpression* Binary(UClass* Class, UMaterialExpression* A, UMaterialExpression* B)
		{
			UMaterialExpression* E = Make(Class);
			Link(A, E, TEXT("A"));
			Link(B, E, TEXT("B"));
			return E;
		}
		UMaterialExpression* Mul(UMaterialExpression* A, UMaterialExpression* B) { return Binary(UMaterialExpressionMultiply::StaticClass(), A, B); }
		UMaterialExpression* Add(UMaterialExpression* A, UMaterialExpression* B) { return Binary(UMaterialExpressionAdd::StaticClass(), A, B); }
		UMaterialExpression* Div(UMaterialExpression* A, UMaterialExpression* B) { return Binary(UMaterialExpressionDivide::StaticClass(), A, B); }
		UMaterialExpression* Min(UMaterialExpression* A, UMaterialExpression* B) { return Binary(UMaterialExpressionMin::StaticClass(), A, B); }
		UMaterialExpression* Max(UMaterialExpression* A, UMaterialExpression* B) { return Binary(UMaterialExpressionMax::StaticClass(), A, B); }
		UMaterialExpression* MulC(UMaterialExpression* A, float B)
		{
			UMaterialExpressionMultiply* E = Make<UMaterialExpressionMultiply>();
			Link(A, E, TEXT("A"));
			E->ConstB = B;
			return E;
		}
		UMaterialExpression* SubC(UMaterialExpression* A, float B)
		{
			UMaterialExpressionSubtract* E = Make<UMaterialExpressionSubtract>();
			Link(A, E, TEXT("A"));
			E->ConstB = B;
			return E;
		}
		UMaterialExpression* Unary(UClass* Class, UMaterialExpression* A)
		{
			UMaterialExpression* E = Make(Class);
			Link(A, E);
			return E;
		}
		UMaterialExpression* OneMinus(UMaterialExpression* A) { return Unary(UMaterialExpressionOneMinus::StaticClass(), A); }
		UMaterialExpression* Saturate(UMaterialExpression* A) { return Unary(UMaterialExpressionSaturate::StaticClass(), A); }
		UMaterialExpression* Frac(UMaterialExpression* A) { return Unary(UMaterialExpressionFrac::StaticClass(), A); }
		UMaterialExpression* Abs(UMaterialExpression* A) { return Unary(UMaterialExpressionAbs::StaticClass(), A); }
		UMaterialExpression* Pow(UMaterialExpression* A, float Exponent)
		{
			UMaterialExpressionPower* E = Make<UMaterialExpressionPower>();
			Link(A, E);
			E->ConstExponent = Exponent;
			return E;
		}
		UMaterialExpression* Mask(UMaterialExpression* A, bool R, bool G, bool B)
		{
			UMaterialExpressionComponentMask* E = Make<UMaterialExpressionComponentMask>();
			Link(A, E);
			E->R = R;
			E->G = G;
			E->B = B;
			E->A = false;
			return E;
		}
		UMaterialExpression* LerpC(float A, float B, UMaterialExpression* Alpha)
		{
			UMaterialExpressionLinearInterpolate* E = Make<UMaterialExpressionLinearInterpolate>();
			E->ConstA = A;
			E->ConstB = B;
			Link(Alpha, E, TEXT("Alpha"));
			return E;
		}
		UMaterialExpression* Lerp(UMaterialExpression* A, UMaterialExpression* B, UMaterialExpression* Alpha)
		{
			UMaterialExpressionLinearInterpolate* E = Make<UMaterialExpressionLinearInterpolate>();
			Link(A, E, TEXT("A"));
			Link(B, E, TEXT("B"));
			Link(Alpha, E, TEXT("Alpha"));
			return E;
		}
		/** 0 at UV center, 0.5 at the edge midpoints. */
		UMaterialExpression* RadialDistance()
		{
			UMaterialExpressionTextureCoordinate* UV = Make<UMaterialExpressionTextureCoordinate>();
			UMaterialExpressionConstant2Vector* C = Make<UMaterialExpressionConstant2Vector>();
			C->R = 0.5f;
			C->G = 0.5f;
			UMaterialExpressionDistance* D = Make<UMaterialExpressionDistance>();
			Link(UV, D, TEXT("A"));
			Link(C, D, TEXT("B"));
			return D;
		}
		/** Per-instance custom data (class is not exported from Engine, so it is created via reflection). */
		UMaterialExpression* CustomData(int32 Index, float Default, bool bVector)
		{
			UClass* Class = StaticLoadClass(UMaterialExpression::StaticClass(), nullptr, bVector ? TEXT("/Script/Engine.MaterialExpressionPerInstanceCustomData3Vector") : TEXT("/Script/Engine.MaterialExpressionPerInstanceCustomData"));
			UMaterialExpression* E = Class ? Make(Class) : nullptr;
			if (!E)
			{
				return Const(Default);
			}
			if (FNumericProperty* P = CastField<FNumericProperty>(Class->FindPropertyByName(TEXT("DataIndex"))))
			{
				P->SetIntPropertyValue(P->ContainerPtrToValuePtr<void>(E), (uint64)Index);
			}
			if (!bVector)
			{
				if (FFloatProperty* P = CastField<FFloatProperty>(Class->FindPropertyByName(TEXT("ConstDefaultValue"))))
				{
					P->SetPropertyValue_InContainer(E, Default);
				}
			}
			else if (FStructProperty* P = CastField<FStructProperty>(Class->FindPropertyByName(TEXT("ConstDefaultValue"))))
			{
				*P->ContainerPtrToValuePtr<FLinearColor>(E) = FLinearColor(Default, Default, Default, 1.f);
			}
			return E;
		}
		UMaterialExpression* InstanceRandom()
		{
			UClass* Class = StaticLoadClass(UMaterialExpression::StaticClass(), nullptr, TEXT("/Script/Engine.MaterialExpressionPerInstanceRandom"));
			return Class ? Make(Class) : Const(0.5f);
		}
		/** Global distance field lookup (class is not exported from Engine). */
		UMaterialExpression* DistanceToNearestSurface(UMaterialExpression* Position)
		{
			UClass* Class = StaticLoadClass(UMaterialExpression::StaticClass(), nullptr, TEXT("/Script/Engine.MaterialExpressionDistanceToNearestSurface"));
			if (!Class)
			{
				return Const(1000.f);
			}
			UMaterialExpression* E = Make(Class);
			Link(Position, E);
			return E;
		}
		/** Passes a vertex-shader-only value (e.g. pre-skinned position) to the pixel shader. */
		UMaterialExpression* Interp(UMaterialExpression* VertexValue)
		{
			UMaterialExpressionVertexInterpolator* E = Make<UMaterialExpressionVertexInterpolator>();
			Link(VertexValue, E, TEXT("VS"));
			return E;
		}
		/** Texture sample parameter; outputs "" (RGB), "R", "G", "B", "A". */
		UMaterialExpression* TexSample(const TCHAR* Name, UTexture* Default, EMaterialSamplerType Type, UMaterialExpression* UV)
		{
			UMaterialExpressionTextureSampleParameter2D* E = Make<UMaterialExpressionTextureSampleParameter2D>();
			E->ParameterName = Name;
			E->Texture = Default;
			E->SamplerType = Type;
			if (UV)
			{
				Link(UV, E, TEXT("UVs"));
			}
			return E;
		}
		/** Scalar from a material parameter collection (shared, set at runtime). */
		UMaterialExpression* Collection(UMaterialParameterCollection* MPC, const TCHAR* Name, float Fallback = 0.f)
		{
			if (!MPC)
			{
				return Const(Fallback);
			}
			UMaterialExpressionCollectionParameter* E = Make<UMaterialExpressionCollectionParameter>();
			E->Collection = MPC;
			E->ParameterName = Name;
			if (const FCollectionScalarParameter* P = MPC->GetScalarParameterByName(Name))
			{
				E->ParameterId = P->Id;
			}
			return E;
		}
		UMaterialExpression* TexObject(const TCHAR* Name, UTexture* Default, EMaterialSamplerType Type)
		{
			UMaterialExpressionTextureObjectParameter* E = Make<UMaterialExpressionTextureObjectParameter>();
			E->ParameterName = Name;
			E->Texture = Default;
			E->SamplerType = Type;
			return E;
		}
		UMaterialExpressionCustom* Custom(const FString& Code, ECustomMaterialOutputType OutputType, const TArray<const TCHAR*>& InputNames, const TArray<TPair<const TCHAR*, ECustomMaterialOutputType>>& ExtraOutputs = {})
		{
			UMaterialExpressionCustom* C = Make<UMaterialExpressionCustom>();
			C->Code = Code;
			C->OutputType = OutputType;
			C->Description = TEXT("Campus");
			C->Inputs.Reset();
			for (const TCHAR* Name : InputNames)
			{
				C->Inputs.AddDefaulted_GetRef().InputName = Name;
			}
			C->AdditionalOutputs.Reset();
			for (const TPair<const TCHAR*, ECustomMaterialOutputType>& Extra : ExtraOutputs)
			{
				FCustomOutput& O = C->AdditionalOutputs.AddDefaulted_GetRef();
				O.OutputName = Extra.Key;
				O.OutputType = Extra.Value;
			}
			C->RebuildOutputs();
			return C;
		}
	};

	inline UMaterial* NewMaterial(const FString& Name, const FString& Folder = TEXT("/Game/Campus/Materials"))
	{
		const FString PackageName = Folder / Name;
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		UMaterial* M = FindObject<UMaterial>(Package, *Name);
		if (M)
		{
			UMaterialEditingLibrary::DeleteAllMaterialExpressions(M);
		}
		else
		{
			UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
			M = Cast<UMaterial>(Factory->FactoryCreateNew(UMaterial::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
			FAssetRegistryModule::AssetCreated(M);
		}
		M->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
		M->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
		return M;
	}

	/** The shared environment collection (weather, lit windows, rain mask), created by the bootstrap. */
	inline UMaterialParameterCollection* EnvironmentCollection()
	{
		return LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Campus/Materials/MPC_Campus_Env.MPC_Campus_Env"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	inline void FinishMaterial(UMaterial* M)
	{
		UMaterialEditingLibrary::LayoutMaterialExpressions(M);
		M->PreEditChange(nullptr);
		M->PostEditChange();
		SaveAsset(M);
	}
}
