/*
    Copyright(c) Microsoft Open Technologies, Inc. All rights reserved.

    The MIT License(MIT)

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files(the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions :

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/

#include "pch.h"
#include "StartupTask.h"
#include <ppltasks.h>
#include "v8.h"
#include "node.h"
#include "Logger.h"
#include <filesystem>

using namespace nodeuwp;
using namespace std::tr2::sys;
using namespace std;

using namespace Platform;
using namespace Windows::ApplicationModel::Background;
using namespace concurrency;
using namespace Windows::UI::Core;
using namespace Windows::Storage;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Platform::Collections;
using namespace Windows::System;

// startupinfo.xml is used by Visual Studio to pass arguments to Node (see StartNode method).
// It's updated through Node.js UWP project properties and packaged in the project appx.
#define STARTUP_FILE L"startupinfo.xml"

// If --use-logger argument is passed to to Node, console.* methods will redirect
// output to a file (nodeuwp.log) in this applications local storage folder.
bool useLogger = false;

std::shared_ptr<char> StartupTask::PlatformStringToChar(const wchar_t* str, int strSize)
{
	// Calculate the needed buffer size
	DWORD bufferSize = WideCharToMultiByte(CP_UTF8,
		0,
		str,
		strSize,
		nullptr,
		0,
		nullptr,
		nullptr);

	if (bufferSize == 0)
	{
		throw ref new ::Platform::Exception(GetLastError(), L"Failed to convert Platform string to utf8 string");
	}

	std::shared_ptr<char> buffer(new char[bufferSize + 1], [](char* ptr) { delete[] ptr; });
	buffer.get()[bufferSize] = '\0';
	// Do the actual conversion
	WideCharToMultiByte(CP_UTF8,
		0,
		str,
		strSize,
		buffer.get(),
		bufferSize,
		nullptr,
		nullptr);

	return buffer;
}

void StartupTask::PopulateArgsVector(std::vector<std::shared_ptr<char>> &argVector, XmlNodeList^ argNodes, bool isStartupScript)
{
	if (argNodes != nullptr)
	{
		IXmlNode^ textNode = argNodes->GetAt(0)->FirstChild;

		if (nullptr == textNode)
			return;

		std::wstring s(((Platform::String^)textNode->NodeValue)->Data());
		std::wstring delimiter = L" ";

		size_t pos = 0;
		std::wstring token;
		std::shared_ptr<char> argChar;
		while ((pos = s.find(delimiter)) != std::wstring::npos) 
		{
			token = s.substr(0, pos);
			argChar = PlatformStringToChar(token.c_str(), token.size());
			if (0 == token.compare(L"--use-logger"))
			{
				useLogger = true;
			}
			else
			{
				argVector.push_back(argChar);
			}			
			s.erase(0, pos + delimiter.length());
		}
		if (0 == s.compare(L"--use-logger"))
		{
			useLogger = true;
			return;
		}

		if (isStartupScript)
		{
			std::wstring localFolder(ApplicationData::Current->LocalFolder->Path->Data());
			localFolder = localFolder.append(L"\\");
			s = localFolder.append(s);
		}

		argChar = PlatformStringToChar(s.c_str(), s.size());
		argVector.push_back(argChar);
	}
}

void StartupTask::CopyFolderSync(StorageFolder^ source, StorageFolder^ destination)
{
	path from(source->Path->Data());
	path to(destination->Path->Data());
	copy_options opts = copy_options::recursive | copy_options::update_existing;
	copy(from, to, opts);
}


void StartupTask::Run(IBackgroundTaskInstance^ taskInstance)
{
	StorageFolder^ appFolder = Windows::ApplicationModel::Package::Current->InstalledLocation;
	StorageFolder^ localFolder = ApplicationData::Current->LocalFolder;
	// Copy files to this applications local storage so that node can read/write to files
	// or folders relative to the location of the starup JavaScript file
	CopyFolderSync(appFolder, localFolder);

	BackgroundTaskDeferral^ deferral = taskInstance->GetDeferral();

	auto installationLocation = Windows::ApplicationModel::Package::Current->InstalledLocation;

	task<StorageFile^> getStartupInfo(installationLocation->GetFileAsync(STARTUP_FILE));

	getStartupInfo.then([=](StorageFile^ storageFile)
	{
		task<XmlDocument^> getStartupInfoXml(XmlDocument::LoadFromFileAsync(storageFile));

		getStartupInfoXml.then([=](XmlDocument^ startupInfoXml)
		{
			std::vector<std::shared_ptr<char>> argumentVector;

			std::shared_ptr<char> argChar = PlatformStringToChar(L" ", 1);
			argumentVector.push_back(argChar);

			XmlNodeList^ argumentNodes = startupInfoXml->SelectNodes(L"StartupInfo/NodeOptions");
			PopulateArgsVector(argumentVector, argumentNodes);

			argumentNodes = startupInfoXml->SelectNodes(L"StartupInfo/Script");
			PopulateArgsVector(argumentVector, argumentNodes, true);

			argumentNodes = startupInfoXml->SelectNodes(L"StartupInfo/ScriptArgs");
			PopulateArgsVector(argumentVector, argumentNodes);


			int argc = argumentVector.size();

			std::shared_ptr<char*> argv;
			argv.reset(new char*[argc], [](char** ptr) { delete[] ptr; });
			for (unsigned int i = 0; i < argumentVector.size(); ++i)
			{
				argv.get()[i] = (argumentVector[i]).get();
			}

			if (!useLogger)
			{
				node::Start(argc, argv.get());
			}
			else
			{
				node::Start(argc, argv.get(), &Logger::GetInstance());
			}
			deferral->Complete();
		});
	});
}
