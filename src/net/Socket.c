/*
 | LuaRT - A Windows programming framework for Lua
 | Luart.org, Copyright (c) Tine Samir 2022.
 | See Copyright Notice in LICENSE.TXT
 |-------------------------------------------------
 | Socket.c | LuaRT Socket object implementation
*/

#include <Socket.h>
#include "lrtapi.h"
#include <luart.h>
#include <Buffer.h>

#include <io.h>
#include <windns.h>

luart_type TSocket;
static const char *socket_mode [] = {"ipv4", "ipv6"};

extern int dns(lua_State *L, const char *str, WORD type);

LUA_CONSTRUCTOR(Socket) {
	Socket *s;
	int mode = lua_optstring(L, 4, socket_mode, 0);
	BOOL resolved = FALSE;
	DWORD len = INET6_ADDRSTRLEN;

	if (lua_islightuserdata(L, -1))
		s = lua_touserdata(L, -1);
	else {
		int port = htons((u_short)luaL_checkinteger(L, 3));
		const char *str = luaL_checkstring(L, 2);
		s = (Socket *)calloc(1, sizeof(Socket));
		s->blocking = TRUE;
addr:	if (inet_pton(AF_INET, str, &(s->addr.sin_addr)) != 0 ) {
			s->addr.sin_family = AF_INET;
			s->addr.sin_port = port;
			s->sizeaddr = sizeof(SOCKADDR_IN);
		} else if (inet_pton(AF_INET6, str, &(s->addr6.sin6_addr)) != 0) {
			s->addr6.sin6_family = AF_INET6;
			s->addr6.sin6_port = port;
			s->sizeaddr = sizeof(SOCKADDR_IN6);
		} else if (!resolved) {
			dns(L, str, mode ? DNS_TYPE_AAAA : DNS_TYPE_A);
			if (lua_isstring(L, -1)) {
				str = lua_tostring(L, -1);
				lua_pop(L, 1);
				resolved = TRUE;
				goto addr;
			}
			luaL_error(L, "could not resolve '%s' to a valid %s address", str,  socket_mode[mode]);
		}
		WSAAddressToStringA((LPSOCKADDR)&s->addr, s->sizeaddr, NULL, s->ip, &len);
		if ( (s->sock = socket(s->sizeaddr == sizeof(SOCKADDR_IN) ? AF_INET : AF_INET6, SOCK_STREAM, 0)) == INVALID_SOCKET ) {
			lasterror(L, WSAGetLastError());
			lua_error(L);
		}
	}

	// ------------------------------------------------- For debugging only
	// hints.ai_family = AF_INET;
    // hints.ai_socktype = SOCK_STREAM;
    // hints.ai_protocol = IPPROTO_TCP;
    // hints.ai_flags = AI_PASSIVE;
	// getaddrinfo(str, NULL, &hints, &servinfo);
	// s->addr = *(SOCKADDR_IN*)servinfo->ai_addr;
	// s->addr.sin_port = port;
	// s->sizeaddr = sizeof(SOCKADDR_IN);
	// s->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	// }
	lua_newinstance(L, s, Socket);
	return 1;
}

static void FillSecBuffer(SecBuffer *buff, unsigned long type, unsigned long count, PBYTE pvBuffer) {
	buff->BufferType = type;
	buff->cbBuffer = count;
	buff->pvBuffer = pvBuffer;
}

static void free_tls(TLS *t) {
	if (t->pCtxtHandle) {
		DeleteSecurityContext(t->pCtxtHandle);
		free(t->pCtxtHandle);
	}
	if (t->pCredHandle) {
		FreeCredentialHandle(t->pCredHandle);
		free(t->pCredHandle);
	}
	if (t->ExtraData.BufferType != SECBUFFER_EMPTY)
		free(t->ExtraData.pvBuffer);
	if (t->RemainingDecryptData.BufferType != SECBUFFER_EMPTY)
		free(t->RemainingDecryptData.pvBuffer);
	if (t->pCertContext != NULL)
		CertFreeCertificateContext(t->pCertContext);
	free(t);
	t = NULL;
}

SECURITY_STATUS DoHandshake(Socket *s, BOOL read, ULONG msTimeout)
{
	SecBufferDesc	InputBufDesc, OutputBufDesc;
	SecBuffer		InputBuf[2], OutputBuf[2];
	DWORD bufSize = 0X10000;
	LPSTR			inpBuf = malloc(bufSize);
	DWORD			bufCount = 0;
	DWORD			dwFlagsRet = 0;
	PCtxtHandle		acceptTemp = NULL;
	SECURITY_STATUS scRet = 0;
	TIMEVAL			tv;
	TLS				*t = s->tls;

	if (inpBuf) {
		scRet = SEC_I_CONTINUE_NEEDED;
		while (scRet == SEC_I_CONTINUE_NEEDED || scRet == SEC_E_INCOMPLETE_MESSAGE) {
			if (bufCount == 0 || scRet == SEC_E_INCOMPLETE_MESSAGE) {
				if (read) {
					fd_set in_set;
					int cnt;
					if (msTimeout > 0) {
						tv.tv_sec = msTimeout / 1000;
						tv.tv_usec = (msTimeout % 1000) * 1000;
					}
					else {
						tv.tv_sec = 10;
						tv.tv_usec = 0;
					}
					FD_ZERO(&in_set);
					FD_SET(s->sock, &in_set);
					cnt = select(0, &in_set, NULL, NULL, &tv);
					if (cnt == 1) {
						int size = recv(s->sock, inpBuf + bufCount, 0x10000 - bufCount, 0);
						if (size <= 0) {
							scRet = SEC_E_INTERNAL_ERROR;
							break;
						}
						bufCount += size;
					}
					else {
						scRet = SEC_E_INTERNAL_ERROR;
						break;
					}
				} else read = TRUE;
			}
			InputBufDesc.cBuffers = 2;
			InputBufDesc.pBuffers = InputBuf;
			InputBufDesc.ulVersion = SECBUFFER_VERSION;
			InputBuf[0].BufferType = SECBUFFER_TOKEN;
			InputBuf[0].pvBuffer = inpBuf;
			InputBuf[0].cbBuffer = bufCount;
			InputBuf[1].BufferType = SECBUFFER_EMPTY;
			InputBuf[1].pvBuffer = NULL;
			InputBuf[1].cbBuffer = 0;
			OutputBufDesc.cBuffers = 2;
			OutputBufDesc.pBuffers = OutputBuf;
			OutputBufDesc.ulVersion = SECBUFFER_VERSION;
			OutputBuf[0].BufferType = SECBUFFER_TOKEN;
			OutputBuf[0].cbBuffer = 0;
			OutputBuf[0].pvBuffer = NULL;
			OutputBuf[1].BufferType = 17;
			OutputBuf[1].cbBuffer = 0;
			OutputBuf[1].pvBuffer = NULL;
			if (s->isServerContext) {
				acceptTemp = (t->acceptSuccess) ? t->pCtxtHandle : NULL;
				scRet = AcceptSecurityContext(t->pCredHandle, acceptTemp, &InputBufDesc, ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_CONFIDENTIALITY | ASC_REQ_STREAM | ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_EXTENDED_ERROR, SECURITY_NATIVE_DREP, t->pCtxtHandle, &OutputBufDesc, &dwFlagsRet, NULL);
			}
			else
				scRet = InitializeSecurityContext(t->pCredHandle, t->pCtxtHandle, NULL, ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM | ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_EXTENDED_ERROR | ISC_REQ_USE_SUPPLIED_CREDS, 0, SECURITY_NATIVE_DREP, &InputBufDesc, 0, NULL, &OutputBufDesc, &dwFlagsRet, NULL);
			if (scRet == SEC_E_OK || scRet == SEC_I_CONTINUE_NEEDED || (FAILED(scRet) && (dwFlagsRet & ISC_REQ_EXTENDED_ERROR)) || scRet == SEC_E_UNTRUSTED_ROOT) {
				if (s->isServerContext && !t->acceptSuccess)
					t->acceptSuccess = TRUE;
				if (OutputBuf[0].cbBuffer != 0 && OutputBuf[0].pvBuffer != NULL) {
					int sent = send(s->sock, (LPCSTR)OutputBuf[0].pvBuffer, OutputBuf[0].cbBuffer, 0);
					FreeContextBuffer(OutputBuf[0].pvBuffer);
					if (OutputBuf[1].pvBuffer != NULL)
						FreeContextBuffer(OutputBuf[1].pvBuffer);
					if (sent == SOCKET_ERROR) {
						free(inpBuf);
						return SEC_E_INTERNAL_ERROR;

					}
				}
			}
			if (scRet == SEC_E_INCOMPLETE_MESSAGE) continue;
			if (scRet == SEC_E_OK || scRet == SEC_E_UNTRUSTED_ROOT) {
				if (InputBuf[1].BufferType == SECBUFFER_EXTRA) {
					if (t->ExtraData.BufferType == SECBUFFER_EMPTY) {
						t->ExtraData.pvBuffer = malloc(InputBuf[1].cbBuffer);
						if (t->ExtraData.pvBuffer == NULL) {
							free(inpBuf);
							return SEC_E_INTERNAL_ERROR;
						}
						MoveMemory(t->ExtraData.pvBuffer, inpBuf + (bufCount - InputBuf[1].cbBuffer), InputBuf[1].cbBuffer);
						t->ExtraData.cbBuffer = InputBuf[1].cbBuffer;
						t->ExtraData.BufferType = SECBUFFER_TOKEN;
					}
					else {
						PVOID newBuf = malloc(InputBuf[1].cbBuffer + t->ExtraData.cbBuffer);
						if (newBuf == NULL)
							return SEC_E_INTERNAL_ERROR;
						CopyMemory(newBuf, t->ExtraData.pvBuffer, t->ExtraData.cbBuffer);
						CopyMemory((char*)newBuf + t->ExtraData.cbBuffer, inpBuf + (bufCount - InputBuf[1].cbBuffer), InputBuf[1].cbBuffer);
						free(t->ExtraData.pvBuffer);
						t->ExtraData.pvBuffer = newBuf;
						t->ExtraData.cbBuffer += InputBuf[1].cbBuffer;
					}
				}
				break;
			}
			if (FAILED(scRet))
				break;
			if (InputBuf[1].BufferType == SECBUFFER_EXTRA) {
				MoveMemory(inpBuf, inpBuf + (bufCount - InputBuf[1].cbBuffer), InputBuf[1].cbBuffer);
				bufCount = InputBuf[1].cbBuffer;
			}
			else
				bufCount = 0;
		}
	}
	free(inpBuf);
	return scRet;
}

LUA_METHOD(Socket, close) {
	Socket *s = lua_self(L, 1, Socket);
	if (s->sock)
		closesocket(s->sock);
	if (s->tls)
		free_tls(s->tls);
	s->sock = INVALID_SOCKET;
	return 0;
}

static int DecryptRecv(Socket *s, char *buffer, ULONG bufLen) {
	SECURITY_STATUS scRet = 0;
	SecBufferDesc MessageBufDesc;
	SecBuffer MsgBuffer[4] = {0};
	PSecBuffer pDataBuf = NULL, pExtraBuf = NULL;
	int decryptBufSize = s->tls->sizes.cbHeader + s->tls->sizes.cbMaximumMessage + s->tls->sizes.cbTrailer;
	int decryptBufUsed = 0;
	PBYTE decryptBuf = NULL;
	int Amount = 0, received, i;
	PSecBuffer pRemnant = &s->tls->RemainingDecryptData;
	PSecBuffer pExtra;

	if (pRemnant->BufferType) {
		int min = min(bufLen, pRemnant->cbBuffer);
		CopyMemory(buffer, pRemnant->pvBuffer, min);
		if (pRemnant->cbBuffer > bufLen) {
			MoveMemory(pRemnant->pvBuffer, (char*)pRemnant->pvBuffer + bufLen, pRemnant->cbBuffer - bufLen);
			pRemnant->cbBuffer = pRemnant->cbBuffer - bufLen;
		} else {
			free(pRemnant->pvBuffer);
			memset(pRemnant, 0, sizeof(SecBuffer));
		}
		return min;
	}
	if ((decryptBuf = calloc(1, decryptBufSize)) == NULL) {
		SetLastError(SEC_E_INSUFFICIENT_MEMORY);
		return SOCKET_ERROR;
	}
	pExtra = &s->tls->ExtraData;
	if (pExtra->BufferType) {
		CopyMemory(decryptBuf, pExtra->pvBuffer, pExtra->cbBuffer);
		decryptBufUsed = pExtra->cbBuffer;
		free(pExtra->pvBuffer);
		memset(pExtra, 0, sizeof(SecBuffer));
	}
	while (TRUE) {
		if (decryptBufUsed == 0 || scRet == SEC_E_INCOMPLETE_MESSAGE) {
			if ((received = recv(s->sock, (LPSTR)decryptBuf + decryptBufUsed, decryptBufSize - decryptBufUsed, 0)) <= 0) {
				Amount = SOCKET_ERROR;
				if (received == 0)
					WSASetLastError(WSAEDISCON);
				break;
			}
			decryptBufUsed += received;
		}
		MessageBufDesc.ulVersion = SECBUFFER_VERSION;
		MessageBufDesc.cBuffers = 4;
		MessageBufDesc.pBuffers = MsgBuffer;
		FillSecBuffer(&MsgBuffer[0], SECBUFFER_DATA, decryptBufUsed, decryptBuf);
		scRet = DecryptMessage(s->tls->pCtxtHandle, &MessageBufDesc, 0, NULL);
		if (scRet == SEC_I_CONTEXT_EXPIRED) break; 
		else if (scRet != SEC_E_OK && scRet != SEC_I_RENEGOTIATE && scRet != SEC_I_CONTEXT_EXPIRED && scRet != SEC_E_INCOMPLETE_MESSAGE) {
			SetLastError(scRet);
			Amount = SOCKET_ERROR;
			break;
		}
		else if (scRet == SEC_E_INCOMPLETE_MESSAGE) continue;
		for (i = 0; i < 4; i++)	{
			if (pDataBuf == NULL && MsgBuffer[i].BufferType == SECBUFFER_DATA)
				pDataBuf = &MsgBuffer[i];
			if (pExtraBuf == NULL && MsgBuffer[i].BufferType == SECBUFFER_EXTRA)
				pExtraBuf = &MsgBuffer[i];
		}
		if (pExtraBuf) {
			pExtra->BufferType = SECBUFFER_EXTRA;
			pExtra->cbBuffer = pExtraBuf->cbBuffer;
			pExtra->pvBuffer = calloc(1, pExtra->cbBuffer);
			CopyMemory(pExtra->pvBuffer, pExtraBuf->pvBuffer, pExtra->cbBuffer);
		}
		if (pDataBuf) {
			ULONG length = pDataBuf->cbBuffer;
			Amount = min(length, bufLen);
			CopyMemory(buffer, pDataBuf->pvBuffer, min(length, bufLen));
			if (length > bufLen) {
				int diff = length - bufLen;
				pRemnant->BufferType = SECBUFFER_DATA;
				pRemnant->cbBuffer = diff;
				if ((pRemnant->pvBuffer = calloc(1, diff)) == NULL) {
					SetLastError(SEC_E_INSUFFICIENT_MEMORY);
					Amount = -2;
					break;
				}
				CopyMemory(pRemnant->pvBuffer, (char*)pDataBuf->pvBuffer + bufLen, diff);
			}
			break;
		}
		if (scRet == SEC_I_RENEGOTIATE && ((scRet = DoHandshake(s, FALSE, 0)) != SEC_E_OK)) {
			SetLastError(scRet);
			Amount = -2;
			break;
		}
	}
	free(decryptBuf);
	return Amount;
}

LUA_METHOD(Socket, recv) {
	Socket *s = lua_self(L, 1, Socket);
	int size = (size_t)luaL_optinteger(L, 2, 1024), done = 0;
	char *buff = malloc(size);
	
	s->read = FALSE;
	if ( s->tls ) {
		if ( (done = DecryptRecv(s, buff+done, size)) == SOCKET_ERROR  )
			goto error;
	}
	else { 
		if ( (done = recv(s->sock, buff, size, 0)) == SOCKET_ERROR ) {
error:		if ( !s->blocking && WSAGetLastError() == WSAEWOULDBLOCK )
				lua_pushnil(L);
			else
				lua_pushboolean(L, FALSE);
			free(buff);
			return 1;
		}
	}
	if (done == 0)
		lua_pushboolean(L, FALSE);
	else {
		lua_pushlstring(L, buff, done);
		lua_pushinstance(L, Buffer, 1);
	}
	free(buff);
	return 1;
}

static int EncryptSend(lua_State *L, Socket *s, const char *message, ULONG msgLen) {
	PSecPkgContext_StreamSizes sizes = &(s->tls->sizes);
	int messageSize = 0;
	int sentBytes = 0;
	//int maxMessageBlobSize = sizes->cbHeader + sizes->cbMaximumMessage + sizes->cbTrailer;
	ULONG i;
	PBYTE sendBuf;
	int sent, sendBytes = 0;

	lua_pushboolean(L, FALSE);
	for (i = 0; i < msgLen; i += sizes->cbMaximumMessage) {
		SecBufferDesc MessageDesc;
		SecBuffer MsgBuffer[4];
		SECURITY_STATUS scRet;

		messageSize = min(sizes->cbMaximumMessage, msgLen - i);
		sendBuf = calloc(1, sizes->cbHeader + messageSize + sizes->cbTrailer);
		if (sendBuf == NULL) {
			sendBytes = -2;
			break;
		}
		MoveMemory(sendBuf + sizes->cbHeader, message + i, messageSize);
		FillSecBuffer(&MsgBuffer[0], SECBUFFER_STREAM_HEADER, sizes->cbHeader, sendBuf);
		FillSecBuffer(&MsgBuffer[1], SECBUFFER_DATA, messageSize, sendBuf + sizes->cbHeader);
		FillSecBuffer(&MsgBuffer[2], SECBUFFER_STREAM_TRAILER, sizes->cbTrailer, sendBuf + sizes->cbHeader + messageSize);
		MsgBuffer[3].BufferType = SECBUFFER_EMPTY;
		MessageDesc.cBuffers = 4;
		MessageDesc.ulVersion = SECBUFFER_VERSION;
		MessageDesc.pBuffers = MsgBuffer;
		if (FAILED(scRet = EncryptMessage(s->tls->pCtxtHandle, 0, &MessageDesc, 0))) {
			SetLastError(scRet);
			sentBytes = SOCKET_ERROR;
			break;
		}
		if ((sent = send(s->sock, (LPSTR)sendBuf, MsgBuffer[0].cbBuffer + MsgBuffer[1].cbBuffer + MsgBuffer[2].cbBuffer, 0)) == SOCKET_ERROR) {
			sentBytes = SOCKET_ERROR;
			break;
		}
		sentBytes += sent;
		free(sendBuf);
	}
	if (sendBytes < 0)
		free(sendBuf);
	return sentBytes;
}

LUA_METHOD(Socket, sendall) {
	size_t len, total = 0;
	Socket *s = lua_self(L, 1, Socket);
	const char *str;
	size_t i;

	str = luaL_tolstring(L, 2, &len);
	s->write = FALSE;
	while(len) {
		str += total;
		switch((i = s->tls ? EncryptSend(L, s, str, len) : send(s->sock, str, len, 0))) {
			case -2 : SetLastError(ERROR_NOT_ENOUGH_MEMORY); goto error;
			case SOCKET_ERROR : if (!s->blocking && WSAGetLastError() == WSAEWOULDBLOCK) {
									lua_pushnil(L);
									return 1;
								} else {
							error:	lua_pushboolean(L, FALSE);
									return 1;
								}	
			default : if (i < len) { total +=i; len -= i; } else goto done;
		}
	}
done:
	lua_pushboolean(L, TRUE);
	return 1;
}


LUA_METHOD(Socket, send) {
	size_t len;
	Socket *s = lua_self(L, 1, Socket);
	const char *str;
	int i;

	str = luaL_tolstring(L, 2, &len);
	s->write = FALSE;
	if (len)
		switch((i = s->tls ? EncryptSend(L, s, str, len) : send(s->sock, str, len, 0))) {
			case SOCKET_ERROR : if (WSAGetLastError() == WSAEWOULDBLOCK) lua_pushnil(L); else lua_pushboolean(L, FALSE); break;
			case -2 : SetLastError(ERROR_NOT_ENOUGH_MEMORY); lua_pushboolean(L, FALSE); break;
			default : lua_pushinteger(L, i);
		}
	else
		lua_pushinteger(L, 0);
	return 1;
}

LUA_METHOD(Socket, start_tls)	{
	SCHANNEL_CRED sc = {0};
	PCCERT_CONTEXT pCertContext = NULL;
	Socket *s = lua_self(L, 1, Socket);
	DWORD err = 0;
	HCERTSTORE hStore = NULL;
	int narg = lua_gettop(L);

	sc.dwVersion = SCHANNEL_CRED_VERSION;
	s->tls = calloc(1, sizeof(TLS));
	s->tls->pCredHandle = calloc(1, sizeof(CredHandle));

	if (narg > 1) {
		if (narg == 2) {
			wchar_t *serverName = lua_towstring(L, 2);
			if ( (hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, X509_ASN_ENCODING, 0, CERT_SYSTEM_STORE_LOCAL_MACHINE|CERT_STORE_READONLY_FLAG, L"ROOT")) == NULL) 
				hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, X509_ASN_ENCODING, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"ROOT");
			pCertContext = CertFindCertificateInStore(hStore, X509_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR, serverName, NULL);
			free(serverName);
		}
		else {
			wchar_t *fileName = lua_towstring(L, 2);
			wchar_t *filePassword = lua_towstring(L, 3);
			FILE *hFile;
			DWORD fileSize = 0;
			DWORD bytesRead = 0;
			LPVOID fileData = NULL;
			CRYPT_DATA_BLOB pfxData;
			PCERT_INFO pCertInfo = NULL;
			PCERT_EXTENSION pCertExtension = NULL;
			DWORD dataLen = 0;
			LPVOID extData = NULL;
			PCERT_BASIC_CONSTRAINTS2_INFO pBC2 = NULL;
			BOOL found = FALSE;
			
			if ((hFile = _wfopen(fileName, L"rb")) != NULL) {
				fseek(hFile, 0, SEEK_END);
				fileSize = (DWORD)_ftelli64(hFile);
				fseek(hFile, 0, SEEK_SET);
				if ((fileData = malloc(fileSize + 1)) != NULL) {
					bytesRead = fread(fileData, 1, fileSize, hFile);
					pfxData.cbData = bytesRead;
					pfxData.pbData = (BYTE *)fileData;
					if ( (hStore = PFXImportCertStore(&pfxData, filePassword, CRYPT_USER_KEYSET )) )
						while (!found && (pCertContext = CertEnumCertificatesInStore(hStore, pCertContext))) {
							pCertInfo = pCertContext->pCertInfo;
							if ((pCertExtension = CertFindExtension(szOID_BASIC_CONSTRAINTS2, pCertInfo->cExtension, pCertInfo->rgExtension)))
							{
								if (!CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, szOID_BASIC_CONSTRAINTS2, pCertExtension->Value.pbData, pCertExtension->Value.cbData, CRYPT_DECODE_NOCOPY_FLAG | CRYPT_DECODE_ALLOC_FLAG, NULL, &extData, &dataLen))
									break;
								pBC2 = (PCERT_BASIC_CONSTRAINTS2_INFO)extData;

								if (!pBC2->fCA) found = TRUE;
								LocalFree(extData);						
							}
						}
				}
			}
			free(fileName);
			free(filePassword);
			free(fileData);
			if (hFile == NULL) 
				luaL_error(L, "certificate not found");
			fclose(hFile);
		}
		if (!pCertContext) {
			err = GetLastError();
			goto error;
		}
		sc.paCred = &pCertContext;
		sc.cCreds = 1;
	}
	if (s->isServerContext) {
		if ((err = AcquireCredentialsHandle(NULL, UNISP_NAME, SECPKG_CRED_INBOUND, NULL, &sc, NULL, NULL, s->tls->pCredHandle, NULL)) == SEC_E_OK) {
			s->tls->pCtxtHandle = calloc(1, sizeof(PCtxtHandle));
			if ( (err = DoHandshake(s, FALSE, 0)) == SEC_E_OK )
				err = QueryContextAttributes(s->tls->pCtxtHandle, SECPKG_ATTR_STREAM_SIZES, &s->tls->sizes);
		}
	}
	else {
		SecBufferDesc	OutputBufDesc = {0};
		SecBuffer		OutputBuf = {0};
		DWORD			dwSSPIOutFlags = 0;

		OutputBufDesc.ulVersion = SECBUFFER_VERSION;
		OutputBufDesc.cBuffers = 1;
		OutputBufDesc.pBuffers = &OutputBuf;
		OutputBuf.BufferType = SECBUFFER_TOKEN;
		OutputBuf.cbBuffer = 0;	
		OutputBuf.pvBuffer = NULL;
		s->tls->pCtxtHandle = malloc(sizeof(CtxtHandle)); 
		sc.dwFlags = (narg == 1 ? SCH_CRED_MANUAL_CRED_VALIDATION : 0) | SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
		if ( (err = AcquireCredentialsHandle( NULL, UNISP_NAME, SECPKG_CRED_OUTBOUND, NULL, &sc, NULL, NULL, s->tls->pCredHandle, NULL)) == SEC_E_OK )
			err = InitializeSecurityContext(s->tls->pCredHandle, NULL, s->ip, (narg == 1 ? ISC_REQ_USE_SUPPLIED_CREDS : 0) | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM | ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_EXTENDED_ERROR, 0, 0, NULL, 0, s->tls->pCtxtHandle, &OutputBufDesc, &dwSSPIOutFlags, NULL);
		if (err == SEC_I_CONTINUE_NEEDED)
			if (OutputBuf.cbBuffer != 0 && OutputBuf.pvBuffer != NULL) {
				if ( send(s->sock, (LPCSTR)OutputBuf.pvBuffer, OutputBuf.cbBuffer, 0) != SOCKET_ERROR)					
					if ( ((err = DoHandshake(s, TRUE, 0)) == SEC_E_OK) && ( (err = QueryContextAttributes(s->tls->pCtxtHandle, SECPKG_ATTR_REMOTE_CERT_CONTEXT, (PVOID)&pCertContext)) ==  SEC_E_OK) )
						err = QueryContextAttributes(s->tls->pCtxtHandle, SECPKG_ATTR_STREAM_SIZES, &s->tls->sizes);
				FreeContextBuffer(OutputBuf.pvBuffer);
			}
	}
	s->tls->pCertContext = pCertContext;
error:
	if (hStore)
		CertCloseStore(hStore, 0);
	if (err)
		SetLastError(err);
	lua_pushboolean(L, err == 0);
	return 1;
}

LUA_METHOD(Socket, connect) {
	Socket *s = lua_self(L, 1, Socket);
	int result = connect(s->sock, (SOCKADDR*)&s->addr, s->sizeaddr);
	result = (result != SOCKET_ERROR) || ((result == SOCKET_ERROR) && (GetLastError() == WSAEWOULDBLOCK)) ? TRUE : FALSE;
	lua_pushboolean(L, result);
	return 1;	
}

LUA_METHOD(Socket, listen) {
	Socket *s = lua_self(L, 1, Socket);
	if (s->addr.sin_family == AF_INET) 
		s->addr.sin_addr.s_addr = htonl(INADDR_ANY);
	else
		s->addr6.sin6_addr = in6addr_any;
	s->isServerContext = TRUE;
	lua_pushboolean(L, bind(s->sock, (SOCKADDR*)&s->addr, s->sizeaddr) == 0 && listen(s->sock, (int)luaL_optinteger(L, 2, 5)) == 0);
	return 1;
}

LUA_METHOD(Socket, accept) {
	SOCKADDR_STORAGE addr;
	SOCKADDR_STORAGE *paddr = &addr;
	int len = sizeof(SOCKADDR_STORAGE);
	Socket *server = lua_self(L, 1, Socket);
	SOCKET accepted = accept(server->sock, (LPSOCKADDR)paddr, &len);
	
	server->read = FALSE;
	if ((int)accepted == SOCKET_ERROR ) {
		if (WSAGetLastError() == WSAEWOULDBLOCK)
			lua_pushnil(L);
		else
			lua_pushboolean(L, FALSE);
	}
	else {
		DWORD size = len;
		Socket *s = (Socket *)calloc(1, sizeof(Socket));
		s->sock = accepted;
		s->isServerContext = TRUE;
		if (addr.ss_family == AF_INET) {
			s->addr = *(SOCKADDR_IN*)paddr;
			s->sizeaddr = sizeof(SOCKADDR_IN);
		}
		else {
			s->addr6 = *(SOCKADDR_IN6*)paddr;
			s->sizeaddr = sizeof(SOCKADDR_IN6);
		}
		WSAAddressToStringA((LPSOCKADDR)&s->addr, s->sizeaddr, NULL, s->ip, &size);
		s->blocking = TRUE;
		lua_pushlightuserdata(L, s);
		lua_pushinstance(L, Socket, 1);
	}
	return 1;
}

LUA_METHOD(Socket, peek) {
	u_long size;
	ioctlsocket(lua_self(L, 1, Socket)->sock, FIONREAD, &size);
	lua_pushinteger(L, (lua_Integer)size);
	return 1;
}

LUA_METHOD(Socket, shutdown) {
	shutdown(lua_self(L, 1, Socket)->sock, SD_BOTH);
	return 0;
}

LUA_PROPERTY_SET(Socket, blocking) {
	Socket *s = lua_self(L, 1, Socket);
	unsigned long mode = !lua_toboolean(L, 2);
	ioctlsocket(s->sock, FIONBIO, &mode);
	s->blocking = mode;
	return 0;
}

LUA_PROPERTY_GET(Socket, blocking) {
	lua_pushboolean(L, lua_self(L, 1, Socket)->blocking);
	return 1;
}

LUA_PROPERTY_GET(Socket, port) {
	Socket *s = lua_self(L, 1, Socket);
	lua_pushinteger(L, ntohs(s->addr.sin_family == AF_INET ? s->addr.sin_port : s->addr6.sin6_port));
	return 1;
}

LUA_PROPERTY_GET(Socket, ip) {
	Socket *s = lua_self(L, 1, Socket);
	if (s->addr.sin_family == AF_INET)
		lua_pushlstring(L, s->ip, strrchr(s->ip, ':')-s->ip);
	else
		lua_pushlstring(L, s->ip+1, strrchr(s->ip, ']')-s->ip-1);
	return 1;
}

LUA_PROPERTY_GET(Socket, family) {
	Socket *s = lua_self(L, 1, Socket);
	lua_pushlstring(L, s->addr.sin_family == AF_INET ? "ipv4" : "ipv6", 4);
	return 1;
}

LUA_PROPERTY_GET(Socket, canread) {
	lua_pushboolean(L, lua_self(L, 1, Socket)->read);
	return 1;
}

LUA_PROPERTY_GET(Socket, canwrite) {
	lua_pushboolean(L, lua_self(L, 1, Socket)->write);
	return 1;
}

LUA_PROPERTY_GET(Socket, haserror) {
	lua_pushboolean(L, lua_self(L, 1, Socket)->error);
	return 1;
}

LUA_METHOD(Socket,__gc)
{
	Socket *s;
	if ((s = lua_self(L, 1, Socket))) {
		if (s->sock != INVALID_SOCKET)
			Socket_close(L);
		free(s);
	}
	return 0;
}

const luaL_Reg Socket_metafields[] = {
	{"__gc",		Socket___gc},
	{NULL, NULL}
};

const luaL_Reg Socket_methods[] = {
	{"accept",			Socket_accept},
	{"close",			Socket_close},
	{"connect",			Socket_connect},
	{"bind",			Socket_listen},
	{"peek",			Socket_peek},
	{"recv",			Socket_recv},
	{"send",			Socket_send},
	{"sendall",			Socket_sendall},
	{"shutdown",		Socket_shutdown},
	{"starttls",		Socket_start_tls},
	{"get_blocking",	Socket_getblocking},
	{"set_blocking",	Socket_setblocking},
	{"get_port",		Socket_getport},
	{"get_ip",			Socket_getip},
	{"get_family",		Socket_getfamily},
	{"get_canread",		Socket_getcanread},
	{"get_canwrite",	Socket_getcanwrite},
	{"get_failed",		Socket_gethaserror},
	{NULL, NULL}
};