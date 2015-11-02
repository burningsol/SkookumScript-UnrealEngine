//=======================================================================================
// SkookumScript Unreal Engine Binding Generator Helper
// Copyright (c) 2015 Agog Labs Inc. All rights reserved.
//
// Author: Markus Breyer
//=======================================================================================

#include <CoreUObject.h>
#include <Regex.h>

#if WITH_EDITOR
  #include "Engine/Blueprint.h"
#endif

//---------------------------------------------------------------------------------------
// This class provides functionality for processing UE4 runtime type information
// and for generating Sk script files

class FSkookumScriptGeneratorBase
  {
  public:

    //---------------------------------------------------------------------------------------
    // Types

    enum eSkTypeID
      {
      SkTypeID_None = 0,
      SkTypeID_Integer,
      SkTypeID_Real,
      SkTypeID_Boolean,
      SkTypeID_String,
      SkTypeID_Vector2,
      SkTypeID_Vector3,
      SkTypeID_Vector4,
      SkTypeID_Rotation,
      SkTypeID_RotationAngles,
      SkTypeID_Transform,
      SkTypeID_Color,
      SkTypeID_Name,
      SkTypeID_Enum,
      SkTypeID_UStruct,
      SkTypeID_UClass,
      SkTypeID_UObject,
      SkTypeID_List,

      SkTypeID__Count
      };

    //---------------------------------------------------------------------------------------
    // Methods

    bool                  compute_scripts_path_depth(FString project_ini_file_path, FString overlay_name);
    bool                  save_text_file_if_changed(const FString & file_path, const FString & new_file_contents); // Helper to change a file only if needed
    void                  flush_saved_text_files(); // Puts generated files into place after all code generation is done

    static bool           is_property_type_supported(UProperty * property_p);
    static bool           is_struct_type_supported(UStruct * struct_p);
    static bool           is_pod(UStruct * struct_p);
    static bool           does_class_have_static_class(UClass * class_p);
    static UEnum *        get_enum(UField * field_p); // Returns the Enum if it is an enum, nullptr otherwise

    static FString        skookify_class_name(const FString & name);
    static FString        skookify_var_name(const FString & name, bool append_question_mark, bool is_member = false);
    static FString        skookify_method_name(const FString & name, UProperty * return_property_p = nullptr);
    static FString        get_skookum_class_name(UStruct * class_or_struct_p);
    FString               get_skookum_class_path(UStruct * class_or_struct_p, FString * out_class_name_p = nullptr);
    FString               get_skookum_method_path(UStruct * class_or_struct_p, const FString & script_function_name, bool is_static);
    static eSkTypeID      get_skookum_struct_type(UStruct * struct_p);
    static eSkTypeID      get_skookum_property_type(UProperty * property_p);
    static FString        get_skookum_property_type_name_existing(UProperty * property_p);
    static uint32         get_skookum_symbol_id(const FString & string);
    static FString        get_comment_block(UField * field_p);

    //---------------------------------------------------------------------------------------
    // Data

    static const FFileHelper::EEncodingOptions::Type  ms_script_file_encoding;

    static const FString  ms_sk_type_id_names[SkTypeID__Count]; // Names belonging to the ids above
    static const FString  ms_reserved_keywords[]; // = Forbidden variable names
    static const FName    ms_meta_data_key_function_category;

    FString               m_scripts_path;       // Folder where to place generated script files
    int32                 m_scripts_path_depth; // Amount of super classes until we start flattening the script file hierarchy due to the evil reign of Windows MAX_PATH. 1 = everything is right under 'Object', 0 is not allowed

    TArray<UStruct *>     m_used_classes;       // All classes used as types (by parameters, properties etc.)
    TArray<FString>       m_temp_file_paths;    // Keep track of temp files generated by save_files_if_changed()
  };

//=======================================================================================
// FSkookumScriptGeneratorBase Implementation
//=======================================================================================

//---------------------------------------------------------------------------------------

const FString FSkookumScriptGeneratorBase::ms_sk_type_id_names[FSkookumScriptGeneratorBase::SkTypeID__Count] =
  {
  TEXT("nil"),
  TEXT("Integer"),
  TEXT("Real"),
  TEXT("Boolean"),
  TEXT("String"),
  TEXT("Vector2"),
  TEXT("Vector3"),
  TEXT("Vector4"),
  TEXT("Rotation"),
  TEXT("RotationAngles"),
  TEXT("Transform"),
  TEXT("Color"),
  TEXT("Name"),
  TEXT("Enum"),
  TEXT("UStruct"),
  TEXT("EntityClass"),  // UClass
  TEXT("Entity"),       // UObject
  TEXT("List"),
  };

const FString FSkookumScriptGeneratorBase::ms_reserved_keywords[] =
  {
  TEXT("branch"),
  TEXT("case"),
  TEXT("divert"),
  TEXT("else"),
  TEXT("exit"),
  TEXT("false"),
  TEXT("fork"),
  TEXT("if"),
  TEXT("loop"),
  TEXT("nil"),
  TEXT("race"),
  TEXT("rush"),
  TEXT("skip"),
  TEXT("sync"),
  TEXT("this"),
  TEXT("this_class"),
  TEXT("this_code"),
  TEXT("true"),
  TEXT("unless"),
  TEXT("when"),

  // Boolean word operators
  TEXT("and"),
  TEXT("nand"),
  TEXT("nor"),
  TEXT("not"),
  TEXT("nxor"),
  TEXT("or"),
  TEXT("xor"),
  };

const FName FSkookumScriptGeneratorBase::ms_meta_data_key_function_category(TEXT("Category"));

const FFileHelper::EEncodingOptions::Type FSkookumScriptGeneratorBase::ms_script_file_encoding = FFileHelper::EEncodingOptions::ForceAnsi;

//---------------------------------------------------------------------------------------

bool FSkookumScriptGeneratorBase::compute_scripts_path_depth(FString project_ini_file_path, FString overlay_name)
  {
  // Try to figure the path depth from ini file
  m_scripts_path_depth = 4; // Set to sensible default in case we don't find it in the ini file
  FString ini_file_text;
  if (FFileHelper::LoadFileToString(ini_file_text, *project_ini_file_path))
    {
    FRegexPattern regex(TEXT("Overlay[0-9]+=-?") + overlay_name + TEXT("\\|.*?\\|([0-9]+)"));
    FRegexMatcher matcher(regex, ini_file_text);
    if (matcher.FindNext())
      {
      int32 begin_idx = matcher.GetCaptureGroupBeginning(1);
      if (begin_idx >= 0)
        {
        int32 path_depth = FCString::Atoi(&ini_file_text[begin_idx]);
        if (path_depth > 0)
          {
          m_scripts_path_depth = path_depth;
          return true;
          }
        }
      }
    }

  return false;
  }

//---------------------------------------------------------------------------------------

bool FSkookumScriptGeneratorBase::save_text_file_if_changed(const FString & file_path, const FString & new_file_contents)
  {
  FString original_file_local;
  FFileHelper::LoadFileToString(original_file_local, *file_path);

  const bool has_changed = original_file_local.Len() == 0 || FCString::Strcmp(*original_file_local, *new_file_contents);
  if (has_changed)
    {
    // save the updated version to a tmp file so that the user can see what will be changing
    const FString temp_file_path = file_path + TEXT(".tmp");

    // delete any existing temp file
    IFileManager::Get().Delete(*temp_file_path, false, true);
    if (!FFileHelper::SaveStringToFile(new_file_contents, *temp_file_path, ms_script_file_encoding))
      {
      FError::Throwf(TEXT("Failed to save file: '%s'"), *temp_file_path);
      }
    else
      {
      m_temp_file_paths.AddUnique(temp_file_path);
      }
    }

  return has_changed;
  }

//---------------------------------------------------------------------------------------

void FSkookumScriptGeneratorBase::flush_saved_text_files()
  {
  // Rename temp headers
  for (auto & temp_file_path : m_temp_file_paths)
    {
    FString file_path = temp_file_path.Replace(TEXT(".tmp"), TEXT(""));
    if (!IFileManager::Get().Move(*file_path, *temp_file_path, true, true))
      {
      FError::Throwf(TEXT("Couldn't write file '%s'"), *file_path);
      }
    }

  m_temp_file_paths.Reset();
  }

//---------------------------------------------------------------------------------------

bool FSkookumScriptGeneratorBase::is_property_type_supported(UProperty * property_p)
  {
  if (property_p->HasAnyPropertyFlags(CPF_EditorOnly)
    || property_p->IsA(ULazyObjectProperty::StaticClass())
    || property_p->IsA(UAssetObjectProperty::StaticClass())
    || property_p->IsA(UAssetClassProperty::StaticClass())
    || property_p->IsA(UWeakObjectProperty::StaticClass()))
    {
    return false;
    }

  return (get_skookum_property_type(property_p) != SkTypeID_None);
  }

//---------------------------------------------------------------------------------------

bool FSkookumScriptGeneratorBase::is_struct_type_supported(UStruct * struct_p)
  {
  UScriptStruct * script_struct = Cast<UScriptStruct>(struct_p);  
  return (script_struct && (script_struct->HasDefaults() || (script_struct->StructFlags & STRUCT_RequiredAPI)));
  }

//---------------------------------------------------------------------------------------

bool FSkookumScriptGeneratorBase::is_pod(UStruct * struct_p)
  {
  UScriptStruct * script_struct = Cast<UScriptStruct>(struct_p);
  return (script_struct && (script_struct->StructFlags & STRUCT_IsPlainOldData));
  }

//---------------------------------------------------------------------------------------

bool FSkookumScriptGeneratorBase::does_class_have_static_class(UClass * class_p)
  {
  return class_p->HasAnyClassFlags(CLASS_RequiredAPI | CLASS_MinimalAPI);
  }

//---------------------------------------------------------------------------------------

UEnum * FSkookumScriptGeneratorBase::get_enum(UField * field_p)
  {
  const UByteProperty * byte_property_p = Cast<UByteProperty>(field_p);
  return byte_property_p ? byte_property_p->Enum : nullptr;
  }

//---------------------------------------------------------------------------------------

FString FSkookumScriptGeneratorBase::skookify_class_name(const FString & name)
  {
  if (name == TEXT("Object")) return TEXT("Entity");
  if (name == TEXT("Class"))  return TEXT("EntityClass");
  if (name == TEXT("Enum"))   return TEXT("Enum2"); // HACK to avoid collision with Skookum built-in Enum class

  // SkookumScript shortcuts for static function libraries as their names occur very frequently in code
  if (name == TEXT("DataTableFunctionLibrary"))          return TEXT("DataLib");
  if (name == TEXT("GameplayStatics"))                   return TEXT("GameLib");
  if (name == TEXT("HeadMountedDisplayFunctionLibrary")) return TEXT("VRLib");
  if (name == TEXT("KismetArrayLibrary"))                return TEXT("ArrayLib");
  if (name == TEXT("KismetGuidLibrary"))                 return TEXT("GuidLib");
  if (name == TEXT("KismetInputLibrary"))                return TEXT("InputLib");
  if (name == TEXT("KismetMaterialLibrary"))             return TEXT("MaterialLib");
  if (name == TEXT("KismetMathLibrary"))                 return TEXT("MathLib");
  if (name == TEXT("KismetNodeHelperLibrary"))           return TEXT("NodeLib");
  if (name == TEXT("KismetStringLibrary"))               return TEXT("StringLib");
  if (name == TEXT("KismetSystemLibrary"))               return TEXT("SystemLib");
  if (name == TEXT("KismetTextLibrary"))                 return TEXT("TextLib");
  if (name == TEXT("VisualLoggerKismetLibrary"))         return TEXT("LogLib");

  return name;
  }

//---------------------------------------------------------------------------------------

FString FSkookumScriptGeneratorBase::skookify_var_name(const FString & name, bool append_question_mark, bool is_member)
  {
  if (name.IsEmpty()) return name;

  // Change title case to lower case with underscores
  FString skookum_name;
  skookum_name.Reserve(name.Len() + 16);
  if (is_member)
    {
    skookum_name.AppendChar('@');
    }
  bool is_boolean = name.Len() > 2 && name[0] == 'b' && isupper(name[1]);
  bool was_upper = true;
  bool was_underscore = true;
  for (int32 i = int32(is_boolean); i < name.Len(); ++i)
    {
    TCHAR c = name[i];

    // Skip special characters
    if (c == TCHAR('?'))
      {
      continue;
      }

    // Insert underscore when appropriate
    if (c == TCHAR(' ') || c == TCHAR(':') || c == TCHAR('_'))
      {
      if (!was_underscore)
        {
        skookum_name.AppendChar('_');
        was_underscore = true;
        }
      }
    else
      {
      bool is_upper = isupper(c) != 0 || isdigit(c) != 0;
      if (is_upper && !was_upper && !was_underscore)
        {
        skookum_name.AppendChar('_');
        }
      skookum_name.AppendChar(tolower(c));
      was_upper = is_upper;
      was_underscore = false;
      }
    }

  // Check for reserved keywords and append underscore if found
  if (!is_member)
    {
    for (uint32 i = 0; i < sizeof(ms_reserved_keywords) / sizeof(ms_reserved_keywords[0]); ++i)
      {
      if (skookum_name == ms_reserved_keywords[i])
        {
        skookum_name.AppendChar('_');
        break;
        }
      }
    }

  // Check if there's an MD5 checksum appended to the name - if so, chop it off
  int32 skookum_name_len = skookum_name.Len();
  if (skookum_name_len > 33)
    {
    const TCHAR * skookum_name_p = &skookum_name[skookum_name_len - 33];
    if (skookum_name_p[0] == TCHAR('_'))
      {
      for (int32 i = 1; i <= 32; ++i)
        {
        uint32_t c = skookum_name_p[i];
        if ((c - '0') > 9u && (c - 'a') > 5u) goto no_md5;
        }
      skookum_name = skookum_name.Left(skookum_name_len - 33);
    no_md5:;
      }
    }

  if (append_question_mark)
    {
    skookum_name.AppendChar(TCHAR('?'));
    }

  return skookum_name;
  }

//---------------------------------------------------------------------------------------

FString FSkookumScriptGeneratorBase::skookify_method_name(const FString & name, UProperty * return_property_p)
  {
  FString method_name = skookify_var_name(name, false);
  bool is_boolean = false;

  // Remove K2 (Kismet 2) prefix if present
  if (method_name.Len() > 3 && !method_name.Mid(3, 1).IsNumeric())
    {
    method_name.RemoveFromStart(TEXT("k2_"), ESearchCase::CaseSensitive);
    }

  if (method_name.Len() > 4 && !method_name.Mid(4, 1).IsNumeric())
    {
    // If name starts with "get_", remove it
    if (method_name.RemoveFromStart(TEXT("get_"), ESearchCase::CaseSensitive))
      {
      // Append question mark
      is_boolean = true;
      }
    // If name starts with "set_", remove it and append "_set" instead
    else if (method_name.RemoveFromStart(TEXT("set_"), ESearchCase::CaseSensitive))
      {
      method_name.Append(TEXT("_set"));
      }
    }

  // If name starts with "is_", "has_" or "can_" also append question mark
  if ((name.Len() > 2 && name[0] == 'b' && isupper(name[1]))
   || method_name.Find(TEXT("is_"), ESearchCase::CaseSensitive) == 0
   || method_name.Find(TEXT("has_"), ESearchCase::CaseSensitive) == 0
   || method_name.Find(TEXT("can_"), ESearchCase::CaseSensitive) == 0)
    {
    is_boolean = true;
    }

  // Append question mark if determined to be boolean
  if (is_boolean && return_property_p && return_property_p->IsA(UBoolProperty::StaticClass()))
    {
    method_name += TEXT("?");
    }

  return method_name;
  }

//---------------------------------------------------------------------------------------

FString FSkookumScriptGeneratorBase::get_skookum_class_name(UStruct * class_or_struct_p)
  {
  return skookify_class_name(class_or_struct_p->GetName());
  }

//---------------------------------------------------------------------------------------

FString FSkookumScriptGeneratorBase::get_skookum_class_path(UStruct * class_or_struct_p, FString * out_class_name_p)
  {
  UClass * class_p = Cast<UClass>(class_or_struct_p);
  bool is_class = (class_p != nullptr);

  // Remember class name
  UObject * obj_p = class_or_struct_p;
  #if WITH_EDITOR
    UBlueprint * blueprint_p = UBlueprint::GetBlueprintFromClass(class_p);
    if (blueprint_p) obj_p = blueprint_p;
  #endif
  FString class_name = skookify_class_name(obj_p->GetName());
  if (out_class_name_p)
    {
    *out_class_name_p = class_name;
    }

  // Make array of the super classes
  TArray<UObject *> super_class_stack;
  super_class_stack.Reserve(32);
  UStruct * super_p = class_or_struct_p;
  while ((super_p = super_p->GetSuperStruct()) != nullptr)
    {
    obj_p = super_p;
    #if WITH_EDITOR
      blueprint_p = UBlueprint::GetBlueprintFromClass(Cast<UClass>(super_p));
      if (blueprint_p) obj_p = blueprint_p;
    #endif
    super_class_stack.Push(obj_p);
    m_used_classes.AddUnique(super_p); // All super classes are also considered used
    }

  // Build path
  int32 max_super_class_nesting = is_class ? FMath::Max(m_scripts_path_depth - 1, 0) : FMath::Max(m_scripts_path_depth - 2, 0);
  FString class_path = m_scripts_path / (is_class ? TEXT("Object") : TEXT("Object/UStruct"));
  for (int32 i = 0; i < max_super_class_nesting && super_class_stack.Num(); ++i)
    {
    class_path /= skookify_class_name(super_class_stack.Pop()->GetName());
    }
  if (super_class_stack.Num())
    {
    class_name = skookify_class_name(super_class_stack[0]->GetName()) + TEXT(".") + class_name;
    }
  return class_path / class_name;
  }

//---------------------------------------------------------------------------------------

FString FSkookumScriptGeneratorBase::get_skookum_method_path(UStruct * class_or_struct_p, const FString & script_function_name, bool is_static)
  {
  return get_skookum_class_path(class_or_struct_p) / (script_function_name.Replace(TEXT("?"), TEXT("-Q")) + (is_static ? TEXT("()C.sk") : TEXT("().sk")));
  }

//---------------------------------------------------------------------------------------

FSkookumScriptGeneratorBase::eSkTypeID FSkookumScriptGeneratorBase::get_skookum_struct_type(UStruct * struct_p)
  {
  static FName name_Vector2D("Vector2D");
  static FName name_Vector("Vector");
  static FName name_Vector_NetQuantize("Vector_NetQuantize");
  static FName name_Vector_NetQuantizeNormal("Vector_NetQuantizeNormal");  
  static FName name_Vector4("Vector4");
  static FName name_Quat("Quat");
  static FName name_Rotator("Rotator");
  static FName name_Transform("Transform");
  static FName name_LinearColor("LinearColor");
  static FName name_Color("Color");

  const FName struct_name = struct_p->GetFName();

  if (struct_name == name_Vector2D)                 return SkTypeID_Vector2;
  if (struct_name == name_Vector)                   return SkTypeID_Vector3;
  if (struct_name == name_Vector_NetQuantize)       return SkTypeID_Vector3;
  if (struct_name == name_Vector_NetQuantizeNormal) return SkTypeID_Vector3;
  if (struct_name == name_Vector4)                  return SkTypeID_Vector4;
  if (struct_name == name_Quat)                     return SkTypeID_Rotation;
  if (struct_name == name_Rotator)                  return SkTypeID_RotationAngles;
  if (struct_name == name_Transform)                return SkTypeID_Transform;
  if (struct_name == name_Color)                    return SkTypeID_Color;
  if (struct_name == name_LinearColor)              return SkTypeID_Color;

  return (is_struct_type_supported(struct_p)) ? SkTypeID_UStruct : SkTypeID_None;
  }

//---------------------------------------------------------------------------------------

FSkookumScriptGeneratorBase::eSkTypeID FSkookumScriptGeneratorBase::get_skookum_property_type(UProperty * property_p)
  {
  // Check for simple types first
  if (property_p->IsA(UNumericProperty::StaticClass()))
    {
    UNumericProperty * numeric_property_p = static_cast<UNumericProperty *>(property_p);
    if (numeric_property_p->IsInteger() && !numeric_property_p->IsEnum())
      {
      return SkTypeID_Integer;
      }
    }
  if (property_p->IsA(UFloatProperty::StaticClass()))       return SkTypeID_Real;
  if (property_p->IsA(UStrProperty::StaticClass()))         return SkTypeID_String;
  if (property_p->IsA(UNameProperty::StaticClass()))        return SkTypeID_Name;
  if (property_p->IsA(UBoolProperty::StaticClass()))        return SkTypeID_Boolean;

  // Any known struct?
  if (property_p->IsA(UStructProperty::StaticClass()))
    {
    UStructProperty * struct_prop_p = CastChecked<UStructProperty>(property_p);
    return get_skookum_struct_type(struct_prop_p->Struct);
    }

  if (get_enum(property_p))                                 return SkTypeID_Enum;
  if (property_p->IsA(UClassProperty::StaticClass()))       return SkTypeID_UClass;

  if (property_p->IsA(UObjectPropertyBase::StaticClass()))
    {
    UClass * class_p = Cast<UObjectPropertyBase>(property_p)->PropertyClass;
    return (does_class_have_static_class(class_p) || class_p->GetName() == TEXT("Object")) ? SkTypeID_UObject : SkTypeID_None;
    }

  if (UArrayProperty * array_property_p = Cast<UArrayProperty>(property_p))
    {
    // Reject arrays of unknown types and arrays of arrays
    return (is_property_type_supported(array_property_p->Inner) && (get_skookum_property_type(array_property_p->Inner) != SkTypeID_List)) ? SkTypeID_List : SkTypeID_None;
    }

  // Didn't find a known type
  return SkTypeID_None;
  }

//---------------------------------------------------------------------------------------

FString FSkookumScriptGeneratorBase::get_skookum_property_type_name_existing(UProperty * property_p)
  {
  eSkTypeID type_id = get_skookum_property_type(property_p);

  if (type_id == SkTypeID_UObject)
    {
    return skookify_class_name(Cast<UObjectPropertyBase>(property_p)->PropertyClass->GetName());
    }
  else if (type_id == SkTypeID_UStruct)
    {
    return skookify_class_name(Cast<UStructProperty>(property_p)->Struct->GetName());
    }
  else if (type_id == SkTypeID_Enum)
    {
    return get_enum(property_p)->GetName();
    }
  else if (type_id == SkTypeID_List)
    {
    return FString::Printf(TEXT("List{%s}"), *get_skookum_property_type_name_existing(Cast<UArrayProperty>(property_p)->Inner));
    }

  return ms_sk_type_id_names[type_id];
  }

//---------------------------------------------------------------------------------------

uint32 FSkookumScriptGeneratorBase::get_skookum_symbol_id(const FString & string)
  {
  char buffer[256];
  char * end_p = FPlatformString::Convert(buffer, sizeof(buffer), *string, string.Len());
  return FCrc::MemCrc32(buffer, end_p - buffer);
  }

//---------------------------------------------------------------------------------------

FString FSkookumScriptGeneratorBase::get_comment_block(UField * field_p)
  {
  #if WITH_EDITOR || HACK_HEADER_GENERATOR
    // Get tool tip from meta data
    FString comment_block = field_p->GetToolTipText().ToString();
    // Convert to comment block
    if (!comment_block.IsEmpty())
      {
      // "Comment out" the comment block
      comment_block = TEXT("// ") + comment_block;
      comment_block.ReplaceInline(TEXT("\n"), TEXT("\n// "));
      comment_block += TEXT("\n");
      // Replace parameter names with their skookified versions
      for (int32 pos = 0;;)
        {
        pos = comment_block.Find(TEXT("@param"), ESearchCase::IgnoreCase, ESearchDir::FromStart, pos);
        if (pos < 0) break;

        pos += 6; // Skip "@param"
        while (FChar::IsWhitespace(comment_block[pos])) ++pos; // Skip white space
        int32 identifier_begin = pos;
        while (FChar::IsIdentifier(comment_block[pos])) ++pos; // Skip identifier
        int32 identifier_length = pos - identifier_begin;
        // Replace parameter name with skookified version
        FString param_name = skookify_var_name(comment_block.Mid(identifier_begin, identifier_length), false);
        comment_block.RemoveAt(identifier_begin, identifier_length, false);
        comment_block.InsertAt(identifier_begin, param_name);
        pos += param_name.Len() - identifier_length;
        }
      }

    // Add original name of this object
    FString this_kind =
      field_p->IsA(UFunction::StaticClass()) ? TEXT("method") :
      (field_p->IsA(UClass::StaticClass()) ? TEXT("class") :
      (field_p->IsA(UStruct::StaticClass()) ? TEXT("struct") :
      (field_p->IsA(UProperty::StaticClass()) ? TEXT("property") :
      (get_enum(field_p) ? TEXT("enum") :
      TEXT("field")))));
    comment_block += FString::Printf(TEXT("//\n// UE4 name of this %s: %s\n"), *this_kind, *field_p->GetName());

    // Add Blueprint category
    if (field_p->HasMetaData(ms_meta_data_key_function_category))
      {
      FString category_name = field_p->GetMetaData(ms_meta_data_key_function_category);
      comment_block += FString::Printf(TEXT("// Blueprint category: %s\n"), *category_name);
      }

    return comment_block + TEXT("\n");
  #else
    return FString();
  #endif
  }
