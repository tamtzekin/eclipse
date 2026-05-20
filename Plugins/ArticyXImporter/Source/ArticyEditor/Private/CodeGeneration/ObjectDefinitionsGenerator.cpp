//  
// Copyright (c) 2023 articy Software GmbH & Co. KG. All rights reserved.  
//

#include "ObjectDefinitionsGenerator.h"
#include "CodeFileGenerator.h"
#include "ObjectDefinitionsImport.h"
#include "ArticyImportData.h"
#include "ArticyEditorModule.h"   // LogArticyEditor

/**
 * @brief Generates code for Articy object definitions based on import data.
 *
 * This function creates header files for object types defined in the Articy project.
 *
 * @param Data The import data used for code generation.
 * @param OutFile The output filename for the generated code.
 */
void ObjectDefinitionsGenerator::GenerateCode(const UArticyImportData* Data, FString& OutFile)
{
	OutFile = CodeGenerator::GetGeneratedTypesFilename(Data);
	CodeFileGenerator(OutFile + ".h", true, [&](CodeFileGenerator* header)
		{
			// ── Reset feature-dedup set ────────────────────────────────────
			// The codegen pipeline runs GenerateCode multiple times during a
			// single reimport (once when the import-data asset is created,
			// then again after "Continuing process"). FeatureTypes persists
			// on the import-data asset across those passes, so on the second
			// pass IsNewFeatureType returns false for every feature and the
			// rewritten .h file ends up with no UCLASS bodies — leaving the
			// generated interfaces/types headers pointing at undefined
			// classes. Reset at the top of each write so every pass emits.
			Data->GetObjectDefs().ResetFeatureTypes();

			header->Line("#include \"CoreUObject.h\"");
			header->Line("#include \"ArticyBaseInclude.h\"");
			header->Line("#include \"" + CodeGenerator::GetGeneratedInterfacesFilename(Data) + ".h\"");
			header->Line("#include \"" + CodeGenerator::GetGeneratedTypesFilename(Data) + ".generated.h\"");
			header->Line();

			// ── Per-type pass (enums first, then templates) ────────────────
			// Per-template features owned directly by a template are emitted
			// here via Template.GenerateFeaturesDefs. This pass also writes
			// all UENUMs (DefType == Enum), which the global feature pass
			// below depends on: features can declare properties of enum type
			// and UHT must have parsed the enum before the feature UCLASS.
			for (const auto& type : Data->GetObjectDefs().GetTypes())
				type.Value.GenerateCode(*header, Data);

			// ── Global feature UCLASS pass ─────────────────────────────────
			// See per-template comment in FArticyObjectDef::GenerateCode for
			// the bug background — features get inherited across templates
			// but the descendant's `Features` array doesn't enumerate them
			// at codegen time, so per-template emission misses them. Emit
			// from the global FeatureDefs map; dedup via IsNewFeatureType
			// (features already written above are skipped). Runs AFTER the
			// per-type loop so enum declarations precede any feature that
			// references them.
			UE_LOG(LogArticyEditor, Verbose,
				TEXT("ObjectDefinitionsGenerator: scanning %d global feature defs for inherited-only emissions"),
				Data->GetObjectDefs().GetFeatures().Num());
			for (const auto& pair : Data->GetObjectDefs().GetFeatures())
			{
				pair.Value.GenerateDefCode(*header, Data);
			}
		});
}
