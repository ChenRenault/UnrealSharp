﻿#include "CSProcHelper.h"
#include "UnrealSharpProcHelper.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/MessageDialog.h"

bool FCSProcHelper::InvokeCommand(const FString& ProgramPath, const FString& Arguments, int32& OutReturnCode, FString& Output, const FString* InWorkingDirectory)
{
	double StartTime = FPlatformTime::Seconds();
	FString ProgramName = FPaths::GetBaseFilename(ProgramPath);

	constexpr bool bLaunchDetached = false;
	constexpr bool bLaunchHidden = true;
	constexpr bool bLaunchReallyHidden = bLaunchHidden;

	void* ReadPipe;
	void* WritePipe;
	FPlatformProcess::CreatePipe(ReadPipe, WritePipe);

	FString WorkingDirectory = InWorkingDirectory ? *InWorkingDirectory : FPaths::GetPath(ProgramPath);
	FProcHandle ProcHandle = FPlatformProcess::CreateProc(*ProgramPath,
														  *Arguments,
														  bLaunchDetached,
														  bLaunchHidden,
														  bLaunchReallyHidden,
														  NULL, 0,
														  *WorkingDirectory,
														  WritePipe,
														  ReadPipe);

	if (!ProcHandle.IsValid())
	{
		FString DialogText = FString::Printf(TEXT("%s failed to launch!"), *ProgramName);
		UE_LOG(LogUnrealSharpProcHelper, Error, TEXT("%s"), *DialogText);

		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(DialogText));
		return false;
	}

	while (FPlatformProcess::IsProcRunning(ProcHandle))
	{
		Output += FPlatformProcess::ReadPipe(ReadPipe);
	}

	FPlatformProcess::GetProcReturnCode(ProcHandle, &OutReturnCode);
	FPlatformProcess::CloseProc(ProcHandle);
	FPlatformProcess::ClosePipe(ReadPipe, WritePipe);

	if (OutReturnCode != 0)
	{
		UE_LOG(LogUnrealSharpProcHelper, Error, TEXT("%s task failed (Args: %s) with return code %d. Error: %s"), *ProgramName, *Arguments, OutReturnCode, *Output)

		FText DialogText = FText::FromString(FString::Printf(TEXT("%s task failed: \n %s"), *ProgramName, *Output));
		FMessageDialog::Open(EAppMsgType::Ok, DialogText);
		return false;
	}

	double EndTime = FPlatformTime::Seconds();
	double ElapsedTime = EndTime - StartTime;
	UE_LOG(LogUnrealSharpProcHelper, Log, TEXT("%s with args (%s) took %f seconds to execute."), *ProgramName, *Arguments, ElapsedTime);

	return true;
}

bool FCSProcHelper::InvokeUnrealSharpBuildTool(const FString& BuildAction, const TMap<FString, FString>& AdditionalArguments)
{
	FString PluginFolder = FPaths::ConvertRelativePathToFull(IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME)->GetBaseDir());
	FString DotNetPath = GetDotNetExecutablePath();

	FString Args;
	Args += FString::Printf(TEXT("\"%s\""), *GetUnrealSharpBuildToolPath());
	Args += FString::Printf(TEXT(" --Action %s"), *BuildAction);
	Args += FString::Printf(TEXT(" --EngineDirectory \"%s\""), *FPaths::ConvertRelativePathToFull(FPaths::EngineDir()));
	Args += FString::Printf(TEXT(" --ProjectDirectory \"%s\""), *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Args += FString::Printf(TEXT(" --ProjectName %s"), FApp::GetProjectName());
	Args += FString::Printf(TEXT(" --PluginDirectory \"%s\""), *PluginFolder);
	Args += FString::Printf(TEXT(" --DotNetPath \"%s\""), *DotNetPath);

	if (AdditionalArguments.Num())
	{
		Args += TEXT(" --AdditionalArgs");
		for (const TPair<FString, FString>& Argument : AdditionalArguments)
		{
			Args += FString::Printf(TEXT(" %s=%s"), *Argument.Key, *Argument.Value);
		}
	}

	int32 ReturnCode = 0;
	FString Output;
	FString WorkingDirectory = GetAssembliesPath();
	return InvokeCommand(DotNetPath, Args, ReturnCode, Output, &WorkingDirectory);
}

FString FCSProcHelper::GetLatestHostFxrPath()
{
	FString DotNetRoot = GetDotNetDirectory();
	FString HostFxrRoot = FPaths::Combine(DotNetRoot, "host", "fxr");

	TArray<FString> Folders;
	IFileManager::Get().FindFiles(Folders, *(HostFxrRoot / "*"), true, true);

	FString HighestVersion = "0.0.0";
	for (const FString &Folder : Folders)
	{
		if (Folder > HighestVersion)
		{
			HighestVersion = Folder;
		}
	}

	if (HighestVersion == "0.0.0")
	{
		UE_LOG(LogUnrealSharpProcHelper, Fatal, TEXT("Failed to find hostfxr version in %s"), *HostFxrRoot);
		return "";
	}

	if (HighestVersion < DOTNET_MAJOR_VERSION)
	{
		UE_LOG(LogUnrealSharpProcHelper, Fatal, TEXT("Hostfxr version %s is less than the required version %s"), *HighestVersion, TEXT(DOTNET_MAJOR_VERSION));
		return "";
	}

#ifdef _WIN32
	return FPaths::Combine(HostFxrRoot, HighestVersion, HOSTFXR_WINDOWS);
#elif defined(__APPLE__)
	return FPaths::Combine(HostFxrRoot, HighestVersion, HOSTFXR_MAC);
#else
	return FPaths::Combine(HostFxrRoot, HighestVersion, HOSTFXR_LINUX);
#endif
}

FString FCSProcHelper::GetRuntimeHostPath()
{
#if WITH_EDITOR
	return GetLatestHostFxrPath();
#else
#ifdef _WIN32
	return FPaths::Combine(GetAssembliesPath(), HOSTFXR_WINDOWS);
#elif defined(__APPLE__)
	return FPaths::Combine(GetAssembliesPath(), HOSTFXR_MAC);
#else
	return FPaths::Combine(GetAssembliesPath(), HOSTFXR_LINUX);
#endif
#endif
}

FString FCSProcHelper::GetPathToSolution()
{
	static FString SolutionPath = GetScriptFolderDirectory() / GetUserManagedProjectName() + ".sln";
	return SolutionPath;
}

FString FCSProcHelper::GetAssembliesPath()
{
#if WITH_EDITOR
	return FPaths::Combine(GetPluginDirectory(), "Binaries", "Managed");
#else
	return GetUserAssemblyDirectory();
#endif
}

FString FCSProcHelper::GetUnrealSharpLibraryPath()
{
	return GetAssembliesPath() / "UnrealSharp.Plugins.dll";
}

FString FCSProcHelper::GetRuntimeConfigPath()
{
	return GetAssembliesPath() / "UnrealSharp.runtimeconfig.json";
}

FString FCSProcHelper::GetUserAssemblyDirectory()
{
	return FPaths::Combine(FPaths::ProjectDir(), "Binaries", "Managed");
}

void FCSProcHelper::GetAllUserAssemblyPaths(TArray<FString>& AssemblyPaths)
{
	FString AbsoluteFolderPath = GetUserAssemblyDirectory();
	IFileManager& FileManager = IFileManager::Get();

	TArray<FString> ProjectPaths;
	GetAllAssemblyPaths(ProjectPaths);

	for (const FString& ProjectPath : ProjectPaths)
	{
		const FString ProjectName = FPaths::GetBaseFilename(ProjectPath);
		const FString MetaDataPath = FPaths::Combine(AbsoluteFolderPath, ProjectName + TEXT(".metadata.json"));
    
		// Continue if the metadata file does not exist.
		if (!FileManager.FileExists(*MetaDataPath))
		{
			continue;
		}

		const FString AssemblyPath = FPaths::Combine(AbsoluteFolderPath, ProjectName + TEXT(".dll"));
		AssemblyPaths.Add(AssemblyPath);
	}
}

void FCSProcHelper::GetAllProjectPaths(TArray<FString>& ProjectPaths)
{
	// Use the FileManager to find files matching the pattern
	IFileManager::Get().FindFilesRecursive(ProjectPaths,
		*GetScriptFolderDirectory(),
		TEXT("*.csproj"),
		true,
		false,
		false);
}

void FCSProcHelper::GetAllAssemblyPaths(TArray<FString>& AssemblyPaths)
{
	// Use the FileManager to find files matching the pattern
	IFileManager::Get().FindFilesRecursive(AssemblyPaths,
		*GetUserAssemblyDirectory(),
		TEXT("*.dll"),
		true,
		false,
		false);
}

FString FCSProcHelper::GetUnrealSharpBuildToolPath()
{
	return FPaths::ConvertRelativePathToFull(GetAssembliesPath() / "UnrealSharpBuildTool.dll");
}

FString FCSProcHelper::GetDotNetDirectory()
{
	const FString PathVariable = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));

	TArray<FString> Paths;
	PathVariable.ParseIntoArray(Paths, FPlatformMisc::GetPathVarDelimiter());

#if defined(_WIN32)
	FString PathDotnet = "Program Files\\dotnet\\";
#elif defined(__APPLE__)
	FString PathDotnet = "/usr/local/share/dotnet/";
	return PathDotnet;
#endif
	for (FString &Path : Paths)
	{
		if (!Path.Contains(PathDotnet))
		{
			continue;
		}

		if (!FPaths::DirectoryExists(Path))
		{
			UE_LOG(LogUnrealSharpProcHelper, Warning, TEXT("Found path to DotNet, but the directory doesn't exist: %s"), *Path);
			break;
		}

		return Path;
	}
	return "";
}

FString FCSProcHelper::GetDotNetExecutablePath()
{
#if defined(_WIN32)
	return GetDotNetDirectory() + "dotnet.exe";
#else
	return GetDotNetDirectory() + "dotnet";
#endif
}

FString& FCSProcHelper::GetPluginDirectory()
{
	static FString PluginDirectory;

	if (PluginDirectory.IsEmpty())
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME);
		check(Plugin);
		PluginDirectory = Plugin->GetBaseDir();
	}

	return PluginDirectory;
}

FString FCSProcHelper::GetUnrealSharpDirectory()
{
	return FPaths::Combine(GetPluginDirectory(), "Managed", "UnrealSharp");
}

FString FCSProcHelper::GetGeneratedClassesDirectory()
{
	return FPaths::Combine(GetUnrealSharpDirectory(), "UnrealSharp", "Generated");
}

FString& FCSProcHelper::GetScriptFolderDirectory()
{
	static FString ScriptFolderDirectory = FPaths::ProjectDir() / "Script";
	return ScriptFolderDirectory;
}

FString FCSProcHelper::GetUserManagedProjectName()
{
	return FString::Printf(TEXT("Managed%s"), FApp::GetProjectName());
}
