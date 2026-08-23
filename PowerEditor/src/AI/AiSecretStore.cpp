// This file is part of Notepad++ project
// Copyright (C)2025 Don HO <don.h@free.fr>

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "AiSecretStore.h"

#include <limits>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>
#endif

namespace NppAi
{
namespace
{

[[nodiscard]] AiStatus validateSecretArguments(std::wstring_view targetName, std::string_view secret, bool requiresSecret)
{
	if (targetName.empty() || targetName.size() > 256 || targetName.find(L'\0') != std::wstring_view::npos)
		return AiMakeError(AiErrorCode::InvalidArgument, "Credential target name is empty, too long, or contains a null character.");
	if (requiresSecret && (secret.empty() || !AiIsValidUtf8(secret)))
		return AiMakeError(AiErrorCode::InvalidUtf8, "Credential secret must be non-empty strict UTF-8.");
	return AiSuccess();
}

} // namespace

AiStatus WindowsCredentialManagerSecretStore::save(std::wstring_view targetName, std::string_view secret)
{
	const AiStatus argumentsStatus = validateSecretArguments(targetName, secret, true);
	if (!argumentsStatus)
		return argumentsStatus;

#ifdef _WIN32
	if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE || secret.size() > std::numeric_limits<DWORD>::max())
		return AiMakeError(AiErrorCode::SizeLimitExceeded, "Credential secret is too large for Windows Credential Manager.");
	std::wstring target(targetName);
	CREDENTIALW credential {};
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = target.data();
	credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
	credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(secret.data()));
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	if (!::CredWriteW(&credential, 0))
		return AiMakeError(AiErrorCode::SecretUnavailable, "Windows Credential Manager could not save the credential.");
	return AiSuccess();
#else
	return AiMakeError(AiErrorCode::UnsupportedPlatform, "Windows Credential Manager is unavailable on this platform.");
#endif
}

AiResult<std::string> WindowsCredentialManagerSecretStore::load(std::wstring_view targetName)
{
	const AiStatus argumentsStatus = validateSecretArguments(targetName, {}, false);
	if (!argumentsStatus)
		return argumentsStatus.error();

#ifdef _WIN32
	std::wstring target(targetName);
	PCREDENTIALW credential = nullptr;
	if (!::CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential) || credential == nullptr)
		return AiMakeError(AiErrorCode::SecretUnavailable, "Windows Credential Manager could not load the credential.");

	std::string secret;
	if (credential->CredentialBlob == nullptr || credential->CredentialBlobSize == 0)
	{
		::CredFree(credential);
		return AiMakeError(AiErrorCode::SecretUnavailable, "Windows Credential Manager returned an empty credential.");
	}
	secret.assign(reinterpret_cast<const char *>(credential->CredentialBlob), credential->CredentialBlobSize);
	::SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
	::CredFree(credential);
	if (!AiIsValidUtf8(secret))
		return AiMakeError(AiErrorCode::SecretUnavailable, "Stored credential is not valid UTF-8.");
	return secret;
#else
	return AiMakeError(AiErrorCode::UnsupportedPlatform, "Windows Credential Manager is unavailable on this platform.");
#endif
}

AiStatus WindowsCredentialManagerSecretStore::erase(std::wstring_view targetName)
{
	const AiStatus argumentsStatus = validateSecretArguments(targetName, {}, false);
	if (!argumentsStatus)
		return argumentsStatus;

#ifdef _WIN32
	std::wstring target(targetName);
	if (!::CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0))
		return AiMakeError(AiErrorCode::SecretUnavailable, "Windows Credential Manager could not erase the credential.");
	return AiSuccess();
#else
	return AiMakeError(AiErrorCode::UnsupportedPlatform, "Windows Credential Manager is unavailable on this platform.");
#endif
}

} // namespace NppAi
