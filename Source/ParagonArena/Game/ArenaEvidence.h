// The evidence journal: the game's own log lines as JSON, one per line, in Saved/Evidence/<id>.jsonl.
// A Shipping build compiles UE_LOG out, so without this the packaged game leaves no trace a test harness can read.
// Off unless the command line carries -EvidenceJournal=<id> (letters, digits, '.', '_' and '-', up to 64): the id is
// the caller's correlation id (Argus's request id, for example) and is written into every line. See docs/ARGUS.md.
#pragma once

#include "CoreMinimal.h"

namespace ArenaEvidence
{
	// true once -EvidenceJournal=<id> was parsed and the file is open (checked once, then cached)
	bool IsOn();
	// one journal line: the log category and verbosity as written in the source, and the formatted message
	void Line(const TCHAR* Category, const TCHAR* Verbosity, const FString& Message);
	// the correlation id, or an empty string when the journal is off
	const FString& Id();
}

// UE_LOG plus the journal. The format arguments are evaluated a second time only when the journal is on.
#define ARENA_LOG(CategoryName, Verbosity, Format, ...) \
	do \
	{ \
		UE_LOG(CategoryName, Verbosity, Format, ##__VA_ARGS__); \
		if (ArenaEvidence::IsOn()) { ArenaEvidence::Line(TEXT(#CategoryName), TEXT(#Verbosity), FString::Printf(Format, ##__VA_ARGS__)); } \
	} while (0)
