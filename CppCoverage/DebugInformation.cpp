#include "stdafx.h"
#include "DebugInformation.hpp"

#include <dbghelp.h>

#include "CppCoverageException.hpp"
#include "IDebugInformationEventHandler.hpp"
#include "Tools.hpp"

namespace CppCoverage
{
	namespace
	{
		struct Context
		{
			Context(
				HANDLE hProcess, 
				DWORD64 baseAddress, 
				void* processBaseOfImage,
				IDebugInformationEventHandler& debugInformationEventHandler)
				: hProcess_(hProcess)
				, baseAddress_(baseAddress)
				, processBaseOfImage_(processBaseOfImage)
				, debugInformationEventHandler_(debugInformationEventHandler)
			{
			}

			HANDLE hProcess_;
			DWORD64 baseAddress_;
			void* processBaseOfImage_;
			IDebugInformationEventHandler& debugInformationEventHandler_;
		};

		BOOL CALLBACK SymEnumLinesProc(PSRCCODEINFO lineInfo, PVOID userContext)
		{
			auto context = static_cast<Context*>(userContext);

			if (!userContext)
				THROW("Invalid user context.");

			DWORD64 address = lineInfo->Address - lineInfo->ModBase + reinterpret_cast<DWORD64>(context->processBaseOfImage_);
			std::wstring filename = Tools::ToWString(lineInfo->FileName);
			context->debugInformationEventHandler_.OnNewLine(filename, lineInfo->LineNumber, address);

			return TRUE;
		}		

		BOOL CALLBACK SymEnumSourceFilesProc(PSOURCEFILE pSourceFile, PVOID userContext)			
		{
			auto context = static_cast<Context*>(userContext);

			if (!userContext)
				THROW("Invalid user context.");
			if (!pSourceFile)
				THROW("Source File is null");
			
			std::wstring filename = Tools::ToWString(pSourceFile->FileName);

			if (context->debugInformationEventHandler_.IsSourceFileSelected(filename))
			{
				if (!SymEnumSourceLines(
					context->hProcess_,
					context->baseAddress_,
					nullptr,
					pSourceFile->FileName,
					0,
					ESLFLAG_FULLPATH,
					SymEnumLinesProc,
					userContext))
				{
					THROW("Cannot enumerate source lines for" << pSourceFile->FileName);
				}
			}

			return TRUE;
		}
	}

	//-------------------------------------------------------------------------
	DebugInformation::DebugInformation(HANDLE hProcess, void* processBaseOfImage)
		: hProcess_(hProcess)
		, processBaseOfImage_(processBaseOfImage)
	{		
		SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES 
			        | SYMOPT_NO_UNQUALIFIED_LOADS | SYMOPT_UNDNAME);

		// $$ test if not found pdb
		// $$ support useer path ?
		if (!SymInitialize(hProcess_, nullptr, FALSE))
			THROW("Error when calling SymInitialize. You cannot call this function twice.");
	}

	//-------------------------------------------------------------------------
	DebugInformation::~DebugInformation()
	{
		if (!SymCleanup(hProcess_))
		{
			// $$ LOG
		}
	}
	
	//-------------------------------------------------------------------------
	void DebugInformation::LoadModule(
		HANDLE hFile,
		IDebugInformationEventHandler& debugInformationEventHandler) const
	{		
		auto baseAddress = SymLoadModuleEx(hProcess_, hFile, nullptr, nullptr, 0, 0, nullptr, 0);

		if (!baseAddress)
			THROW("Cannot load module."); // $$ use path instead of hFile to have better error ?
		
		try
		{
			Context context{hProcess_, baseAddress, processBaseOfImage_, debugInformationEventHandler };
			
			if (!SymEnumSourceFiles(hProcess_, baseAddress, nullptr, SymEnumSourceFilesProc, &context))
				THROW("Cannot enumerate source files");			

			UnloadModule64(baseAddress);			
		}
		catch (...)
		{
			UnloadModule64(baseAddress);
			throw;
		}		
	}

	//-------------------------------------------------------------------------
	void DebugInformation::UnloadModule64(DWORD64 baseOfDll) const
	{
		if (!SymUnloadModule64(hProcess_, baseOfDll))
			THROW("Cannot unload module");
	}

}
