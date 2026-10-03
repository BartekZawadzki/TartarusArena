#include "Game/ArenaEvidence.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace
{
	struct FJournal
	{
		bool bChecked = false;
		bool bOn = false;
		FString Id;
		int64 Seq = 0;
		TUniquePtr<FArchive> File;
		FCriticalSection Lock;
	};

	FJournal& Journal()
	{
		static FJournal J;
		return J;
	}

	bool ValidId(const FString& S)
	{
		if (S.IsEmpty() || S.Len() > 64) { return false; }
		for (const TCHAR C : S)
		{
			if (!(FChar::IsAlnum(C) || C == TEXT('.') || C == TEXT('_') || C == TEXT('-'))) { return false; }
		}
		return true;
	}

	FString Escape(const FString& S)
	{
		FString Out;
		Out.Reserve(S.Len() + 8);
		for (const TCHAR C : S)
		{
			switch (C)
			{
			case TEXT('"'): Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default:
				if (C < 0x20) { Out += FString::Printf(TEXT("\\u%04x"), (int32)C); }
				else { Out.AppendChar(C); }
			}
		}
		return Out;
	}

	// the first word of a message names its kind: LAB, LAB_SUMMARY, ARENA_SUMMARY, DEATH_AUDIT…; "ARENA evt=x" -> x
	FString EventOf(const FString& Msg)
	{
		FString Head, Rest;
		if (!Msg.Split(TEXT(" "), &Head, &Rest)) { Head = Msg; }
		if (Head == TEXT("ARENA") && Rest.StartsWith(TEXT("evt=")))
		{
			FString Evt = Rest.Mid(4);
			int32 Space = INDEX_NONE;
			if (Evt.FindChar(TEXT(' '), Space)) { Evt.LeftInline(Space); }
			return Evt;
		}
		return Head.Left(48);
	}

	const TCHAR* LevelOf(const TCHAR* Verbosity)
	{
		if (FCString::Strcmp(Verbosity, TEXT("Error")) == 0 || FCString::Strcmp(Verbosity, TEXT("Fatal")) == 0) { return TEXT("error"); }
		if (FCString::Strcmp(Verbosity, TEXT("Warning")) == 0) { return TEXT("warn"); }
		return TEXT("info");
	}

	void Write(FJournal& J, const FString& Json)
	{
		if (!J.File) { return; }
		FTCHARToUTF8 Utf8(*(Json + TEXT("\n")));
		J.File->Serialize((void*)Utf8.Get(), Utf8.Length());
		J.File->Flush();
	}

	FString Build()
	{
#if UE_BUILD_SHIPPING
		return TEXT("Shipping");
#elif UE_BUILD_DEVELOPMENT
		return TEXT("Development");
#else
		return TEXT("Debug");
#endif
	}

	void Close()
	{
		FJournal& J = Journal();
		FScopeLock Guard(&J.Lock);
		if (!J.File) { return; }
		const int64 N = ++J.Seq;
		Write(J, FString::Printf(TEXT("{\"ts\":\"%s\",\"requestId\":\"%s\",\"service\":\"tartarus-arena\",\"source\":\"game\",\"level\":\"info\",\"event_type\":\"saga\",\"event\":\"EVIDENCE_CLOSE\",\"seq\":%lld,\"msg\":\"EVIDENCE_CLOSE lines=%lld\"}"),
			*FDateTime::UtcNow().ToIso8601(), *J.Id, N, N));
		J.File->Close();
		J.File.Reset();
	}

	void Open(FJournal& J)
	{
		J.bChecked = true;
		FString Id;
		if (!FParse::Value(FCommandLine::Get(), TEXT("EvidenceJournal="), Id) || !ValidId(Id)) { return; }
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Evidence") / (Id + TEXT(".jsonl"));
		J.File.Reset(IFileManager::Get().CreateFileWriter(*Path, FILEWRITE_Append | FILEWRITE_AllowRead));
		if (!J.File) { return; }
		J.Id = Id;
		J.bOn = true;
		Write(J, FString::Printf(TEXT("{\"ts\":\"%s\",\"requestId\":\"%s\",\"service\":\"tartarus-arena\",\"source\":\"game\",\"level\":\"info\",\"event_type\":\"saga\",\"event\":\"EVIDENCE_OPEN\",\"seq\":%lld,\"build\":\"%s\",\"msg\":\"EVIDENCE_OPEN build=%s\"}"),
			*FDateTime::UtcNow().ToIso8601(), *J.Id, ++J.Seq, *Build(), *Build()));
		FCoreDelegates::OnExit.AddStatic(&Close);
	}
}

bool ArenaEvidence::IsOn()
{
	FJournal& J = Journal();
	FScopeLock Guard(&J.Lock);
	if (!J.bChecked) { Open(J); }
	return J.bOn && J.File.IsValid();
}

const FString& ArenaEvidence::Id()
{
	return Journal().Id;
}

void ArenaEvidence::Line(const TCHAR* Category, const TCHAR* Verbosity, const FString& Message)
{
	FJournal& J = Journal();
	FScopeLock Guard(&J.Lock);
	if (!J.File) { return; }
	Write(J, FString::Printf(TEXT("{\"ts\":\"%s\",\"requestId\":\"%s\",\"service\":\"tartarus-arena\",\"source\":\"game\",\"level\":\"%s\",\"logger\":\"%s\",\"event_type\":\"log\",\"event\":\"%s\",\"seq\":%lld,\"msg\":\"%s\"}"),
		*FDateTime::UtcNow().ToIso8601(), *J.Id, LevelOf(Verbosity), Category, *Escape(EventOf(Message)), ++J.Seq, *Escape(Message)));
}
