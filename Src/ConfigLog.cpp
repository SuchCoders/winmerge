/////////////////////////////////////////////////////////////////////////////
//    License (GPLv2+):
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or (at
//    your option) any later version.
//    
//    This program is distributed in the hope that it will be useful, but
//    WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
/////////////////////////////////////////////////////////////////////////////
/** 
 * @file  ConfigLog.cpp
 *
 * @brief CConfigLog implementation
 */

#include "ConfigLog.h"
#include <cassert>
#include <windows.h>
#include <mbctype.h>
#include <memory>
#include "Constants.h"
#include "VersionInfo.h"
#include "UniFile.h"
#include "Plugins.h"
#include "TFile.h"
#include "paths.h"
#include "locality.h"
#include "unicoder.h"
#include "Environment.h"
#include "MergeApp.h"
#include "OptionsMgr.h"
#include "TempFile.h"
#include "UniFile.h"
#include "RegKey.h"

CConfigLog::CConfigLog()
: m_pfile(new UniStdioFile())
{
}

CConfigLog::~CConfigLog()
{
	CloseFile();
}



/** 
 * @brief Return logfile name and path
 */
String CConfigLog::GetFileName() const
{
	return m_sFileName;
}


static String GetLastModified(const String &path) 
{
	String sPath2 = path;
	if (sPath2[0] == '.')
	{
		CVersionInfo EXEversion;
		String sEXEPath = paths::GetPathOnly(paths::GetLongPath(EXEversion.GetFullFileName(), false));
		sPath2 = sEXEPath + _T("\\") + sPath2;
	}
	TFile file(sPath2);

	String sModifiedTime = _T("");
	if (file.exists())
	{
		Poco::Timestamp mtime(file.getLastModified());

		const int64_t r = (mtime.epochTime());
		sModifiedTime = locality::TimeString(&r);
	}
	return sModifiedTime;
}

/** 
 * @brief Write plugin names
 */
void CConfigLog::WritePluginsInLogFile(const wchar_t *transformationEvent)
{
	// get an array with the available scripts
	PluginArray * piPluginArray; 

	piPluginArray = 
		CAllThreadsScripts::GetActiveSet()->GetAvailableScripts(transformationEvent);

	for (size_t iPlugin = 0 ; iPlugin < piPluginArray->size() ; iPlugin++)
	{
		const PluginInfoPtr& plugin = piPluginArray->at(iPlugin);
		m_pfile->WriteString(_T("\r\n   "));
		if (plugin->m_disabled)
			m_pfile->WriteString(_T("!"));
		m_pfile->WriteString(plugin->m_name);
		m_pfile->WriteString(_T("  path="));
		m_pfile->WriteString(plugin->m_filepath);
	}
}

/**
 * @brief String wrapper around API call GetLocaleInfo
 */
static String GetLocaleString(LCID locid, LCTYPE lctype)
{
	TCHAR buffer[512];
	if (!GetLocaleInfo(locid, lctype, buffer, sizeof(buffer)/sizeof(buffer[0])))
		buffer[0] = 0;
	return buffer;
}

/**
 * @brief Write string item
 */
void CConfigLog::WriteItem(int indent, const String& key, const TCHAR *value)
{
	String text = strutils::format(value ? _T("%*.0s%s: %s\r\n") : _T("%*.0s%s:\r\n"), indent, key.c_str(), key.c_str(), value);
	m_pfile->WriteString(text);
}

/**
 * @brief Write string item
 */
void CConfigLog::WriteItem(int indent, const String& key, const String &str)
{
	WriteItem(indent, key, str.c_str());
}

/**
 * @brief Write int item
 */
void CConfigLog::WriteItem(int indent, const String& key, long value)
{
	String text = strutils::format(_T("%*.0s%s: %ld\r\n"), indent, key.c_str(), key.c_str(), value);
	m_pfile->WriteString(text);
}

/**
 * @brief Write out various possibly relevant windows locale information
 */
void CConfigLog::WriteLocaleSettings(unsigned locid, const String& title)
{
	WriteItem(1, title);
	WriteItem(2, _T("Def ANSI codepage"), GetLocaleString(locid, LOCALE_IDEFAULTANSICODEPAGE));
	WriteItem(2, _T("Def OEM codepage"), GetLocaleString(locid, LOCALE_IDEFAULTCODEPAGE));
	WriteItem(2, _T("Country"), GetLocaleString(locid, LOCALE_SENGCOUNTRY));
	WriteItem(2, _T("Language"), GetLocaleString(locid, LOCALE_SENGLANGUAGE));
	WriteItem(2, _T("Language code"), GetLocaleString(locid, LOCALE_ILANGUAGE));
	WriteItem(2, _T("ISO Language code"), GetLocaleString(locid, LOCALE_SISO639LANGNAME));
}

/**
 * @brief Write version of a single executable file
 */
void CConfigLog::WriteVersionOf1(int indent, const String& path)
{
	String path2 = path;
	if (path2.find(_T(".\\")) == 0)
	{
		// Remove "relative path" info for Win API calls.
		const TCHAR *pf = path2.c_str();
		path2 = String(pf+2);
	}
	String name = paths::FindFileName(path2);
	CVersionInfo vi(path2.c_str(), TRUE);
	String sModifiedTime = _T("");
	if (name != path)
	{
		sModifiedTime = GetLastModified(path);
		if (!sModifiedTime.empty())
			sModifiedTime = _T("  [") + sModifiedTime + _T("]");
	}
	String text = strutils::format
	(
		name == path
			?	_T(" %*s%-19s %s=%u.%02u %s=%04u\r\n")
			:	_T(" %*s%-19s %s=%u.%02u %s=%04u path=%s%s\r\n"),
		indent,
		// Tilde prefix for modules currently mapped into WinMerge
		GetModuleHandle(path2.c_str()) 
			? _T("~") 
			: _T("")/*name*/,
		name.c_str(),
		vi.m_dvi.cbSize > FIELD_OFFSET(DLLVERSIONINFO, dwMajorVersion)
			?	_T("dllversion")
			:	_T("version"),
		vi.m_dvi.dwMajorVersion,
		vi.m_dvi.dwMinorVersion,
		vi.m_dvi.cbSize > FIELD_OFFSET(DLLVERSIONINFO, dwBuildNumber)
			?	_T("dllbuild")
			:	_T("build"),
		vi.m_dvi.dwBuildNumber,
		path.c_str(),
		sModifiedTime.c_str()
	);
	m_pfile->WriteString(text);
}

/**
 * @brief Write winmerge configuration
 */
void CConfigLog::WriteWinMergeConfig()
{
	TempFile tmpfile;
	tmpfile.Create();
	GetOptionsMgr()->ExportOptions(tmpfile.GetPath(), true);
	UniMemFile ufile;
	if (!ufile.OpenReadOnly(tmpfile.GetPath()))
		return;
	String line;
	bool lossy;
	while (ufile.ReadString(line, &lossy)) 
	{
		String prefix = _T("  ");
		if (line[0] == _T('[') )
			prefix = _T(" ");
		FileWriteString(prefix + line + _T("\r\n"));
	}
	ufile.Close();
}

/** 
 * @brief Write logfile
 */
bool CConfigLog::DoFile(String &sError)
{
	CVersionInfo version;
	String text;

	String sFileName = paths::ConcatPath(env::GetMyDocuments(), WinMergeDocumentsFolder);
	paths::CreateIfNeeded(sFileName);
	m_sFileName = paths::ConcatPath(sFileName, _T("WinMerge.txt"));

	if (!m_pfile->OpenCreateUtf8(m_sFileName))
	{
		const UniFile::UniError &err = m_pfile->GetLastUniError();
		sError = err.GetError();
		return false;
	}
	m_pfile->SetBom(true);
	m_pfile->WriteBom();

// Begin log
	FileWriteString(_T("WinMerge Configuration Log\r\n"));
	FileWriteString(_T("--------------------------\r\n"));
	FileWriteString(_T("Saved to: "));
	FileWriteString(m_sFileName);
	FileWriteString(_T("\r\n* Please add this information (or attach this file) when reporting bugs."));

// Platform stuff
	
	FileWriteString(_T("\r\n\r\nWindows Info:         "));
	text = GetWindowsVer();
	FileWriteString(text);
	text = GetProcessorInfo();
	if (text != _T(""))
	{		
		FileWriteString(_T("\r\n Processor:           "));
		FileWriteString(text);
	}

// WinMerge stuff

	FileWriteString(_T("\r\n\r\nWinMerge Info:"));
	String sEXEFullFileName = paths::GetLongPath(version.GetFullFileName(), false);
	FileWriteString(_T("\r\n Code File:           "));
	FileWriteString(sEXEFullFileName);
	FileWriteString(_T("\r\n Version:             "));
	FileWriteString(version.GetProductVersion());

	String privBuild = version.GetPrivateBuild();
	if (!privBuild.empty())
	{
		FileWriteString(_T("  (Private Build) "));
	}

	String sModifiedTime = GetLastModified(sEXEFullFileName);
	FileWriteString(_T("\r\n DateTime Modified:   "));
	FileWriteString(sModifiedTime);

	text = GetBuildFlags();
	FileWriteString(_T("\r\n Build config:       "));
	FileWriteString(text);

	LPCTSTR szCmdLine = ::GetCommandLine();
	assert(szCmdLine != NULL);

	// Skip the quoted executable file name.
	if (szCmdLine != NULL)
	{
		szCmdLine = _tcschr(szCmdLine, '"');
		if (szCmdLine != NULL)
		{
			szCmdLine += 1; // skip the opening quote.
			szCmdLine = _tcschr(szCmdLine, '"');
			if (szCmdLine != NULL)
			{
				szCmdLine += 1; // skip the closing quote.
			}
		}
	}

	// The command line include a space after the executable file name,
	// which mean that empty command line will have length of one.
	if (!szCmdLine || lstrlen(szCmdLine) < 2)
	{
		szCmdLine = _T(" none");
	}

	FileWriteString(_T("\r\n\r\nCommand Line:        "));
	FileWriteString(szCmdLine);

	String sEXEPathOnly = paths::GetPathOnly(sEXEFullFileName);

	FileWriteString(_T("\r\n\r\nModule Names:         Tilda (~) prefix indicates currently loaded into the WinMerge process.\r\n"));
	WriteVersionOf1(1, _T("kernel32.dll"));
	WriteVersionOf1(1, _T("shell32.dll"));
	WriteVersionOf1(1, _T("shlwapi.dll"));
	WriteVersionOf1(1, _T("COMCTL32.dll"));
	FileWriteString(_T(        "                      These path names are relative to the Code File's directory.\r\n"));
	WriteVersionOf1(1, _T(".\\ShellExtensionU.dll"));
	WriteVersionOf1(1, _T(".\\ShellExtensionX64.dll"));
	WriteVersionOf1(1, _T(".\\MergeLang.dll"));
	WriteVersionOf1(1, _T(".\\Frhed\\hekseditU.dll"));
	WriteVersionOf1(1, _T(".\\WinIMerge\\WinIMergeLib.dll"));
	WriteVersionOf1(1, _T(".\\Merge7z\\7z.dll"));

// System settings
	FileWriteString(_T("\r\nSystem Settings:\r\n"));
	FileWriteString(_T(" Codepage Settings:\r\n"));
	WriteItem(2, _T("ANSI codepage"), GetACP());
	WriteItem(2, _T("OEM codepage"), GetOEMCP());
#ifndef UNICODE
	WriteItem(2, _T("multibyte codepage"), _getmbcp());
#endif
	WriteLocaleSettings(GetThreadLocale(), _T("Locale (Thread)"));
	WriteLocaleSettings(LOCALE_USER_DEFAULT, _T("Locale (User)"));
	WriteLocaleSettings(LOCALE_SYSTEM_DEFAULT, _T("Locale (System)"));

// Plugins
	FileWriteString(_T("\r\nPlugins:\r\n"));
	FileWriteString(_T(" Unpackers: "));
	WritePluginsInLogFile(L"FILE_PACK_UNPACK");
	WritePluginsInLogFile(L"BUFFER_PACK_UNPACK");
	WritePluginsInLogFile(L"FILE_FOLDER_PACK_UNPACK");
	FileWriteString(_T("\r\n Prediffers: "));
	WritePluginsInLogFile(L"FILE_PREDIFF");
	WritePluginsInLogFile(L"BUFFER_PREDIFF");
	FileWriteString(_T("\r\n Editor scripts: "));
	WritePluginsInLogFile(L"EDITOR_SCRIPT");
	if (plugin::IsWindowsScriptThere() == FALSE)
		FileWriteString(_T("\r\n .sct scripts disabled (Windows Script Host not found)\r\n"));

	FileWriteString(_T("\r\n\r\n"));

// WinMerge settings
	FileWriteString(_T("\r\nWinMerge configuration:\r\n"));
	WriteWinMergeConfig();

	CloseFile();

	return true;
}

/** 
 * @brief Parse Windows version data to string.
 * @return String describing Windows version.
 */
String CConfigLog::GetWindowsVer() const
{
	CRegKeyEx key;
	if (key.QueryRegMachine(_T("Software\\Microsoft\\Windows NT\\CurrentVersion")))
		return key.ReadString(_T("ProductName"), _T("Unknown OS"));
	return _T("Unknown OS");
}


/** 
 * @brief Parse Processor Information data to string.
 * @return String describing Windows version.
 */
String CConfigLog::GetProcessorInfo() const
{
	CRegKeyEx key;
	String sProductName = _T("");
	if (key.QueryRegMachine(_T("Hardware\\Description\\System\\CentralProcessor\\0")))
		sProductName = key.ReadString(_T("Identifier"), _T(""));
	if (sProductName != _T(""))
	{
		// This is the full identifier of the processor
		//	(e.g. "Intel64 Family 6 Model 158 Stepping 9")
		//	but we'll only keep the first word (e.g. "Intel64")
		int x = (int)sProductName.find_first_of(_T(" "));
		sProductName = sProductName.substr(0, x);
	}


	// Number of processors, Amount of memory
	SYSTEM_INFO siSysInfo;
	::GetSystemInfo(&siSysInfo); 

	MEMORYSTATUSEX GlobalMemoryBuffer;
	memset(&GlobalMemoryBuffer, 0, sizeof(GlobalMemoryBuffer));
	GlobalMemoryBuffer.dwLength = sizeof (GlobalMemoryBuffer);
	::GlobalMemoryStatusEx(&GlobalMemoryBuffer);
	ULONG lInstalledMemory = (ULONG)(GlobalMemoryBuffer.ullTotalPhys / (1024*1024));

	TCHAR buf[MAX_PATH];
	swprintf_s(buf, MAX_PATH, _T("%u Logical Processors, %u MB Memory"), 
			siSysInfo.dwNumberOfProcessors, lInstalledMemory); 

	return sProductName + _T(", ") + String(buf);
}
	
/** 
 * @brief Return string representation of build flags (for reporting in config log)
 */
String CConfigLog::GetBuildFlags() const
{
	String flags;

#if defined WIN64
	flags += _T(" WIN64 ");
#elif defined WIN32
	flags += _T(" WIN32 ");
#endif

#if defined UNICODE
	flags += _T(" UNICODE ");
#endif

#if defined _DEBUG
	flags += _T(" _DEBUG ");
#endif

#if defined TEST_WINMERGE
	flags += _T(" TEST_WINMERGE ");
#endif

	return flags;
}

bool CConfigLog::WriteLogFile(String &sError)
{
	CloseFile();

	return DoFile(sError);
}

/// Write line to file (if writing configuration log)
void
CConfigLog::FileWriteString(const String& lpsz)
{
	m_pfile->WriteString(lpsz);
}

/**
 * @brief Close any open file
 */
void
CConfigLog::CloseFile()
{
	if (m_pfile->IsOpen())
		m_pfile->Close();
}

