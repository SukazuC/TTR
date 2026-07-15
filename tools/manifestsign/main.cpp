#include "sha256.h"
#include <Windows.h>
#include <bcrypt.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool Read(const char*path,std::vector<std::byte>&bytes,size_t limit=1024*1024){std::ifstream file(path,std::ios::binary);if(!file)return false;file.seekg(0,std::ios::end);auto size=file.tellg();file.seekg(0);if(size<=0||size>static_cast<std::streamoff>(limit))return false;bytes.resize(static_cast<size_t>(size));file.read(reinterpret_cast<char*>(bytes.data()),size);return !!file;}
bool Write(const char*path,const void*data,size_t size){std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(static_cast<const char*>(data),static_cast<std::streamsize>(size));return !!file;}
bool Export(BCRYPT_KEY_HANDLE key,const wchar_t*type,const char*path){ULONG bytes{};if(BCryptExportKey(key,nullptr,type,nullptr,0,&bytes,0)<0)return false;std::vector<UCHAR>blob(bytes);return BCryptExportKey(key,nullptr,type,blob.data(),bytes,&bytes,0)>=0&&Write(path,blob.data(),bytes);}
}

int main(int argc,char**argv){
  if(argc==4&&std::string(argv[1])=="generate"){
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_KEY_HANDLE key{};bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0&&BCryptGenerateKeyPair(algorithm,&key,256,0)>=0&&BCryptFinalizeKeyPair(key,0)>=0&&Export(key,BCRYPT_ECCPRIVATE_BLOB,argv[2])&&Export(key,BCRYPT_ECCPUBLIC_BLOB,argv[3]);if(key)BCryptDestroyKey(key);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);if(!ok)std::cerr<<"manifestsign: key generation failed\n";return ok?0:1;
  }
  if(argc==4&&std::string(argv[1])=="sign"){
    std::vector<std::byte>keyBlob,data;if(!Read(argv[2],keyBlob)||!Read(argv[3],data)){std::cerr<<"manifestsign: unable to read input\n";return 1;}ttr::Sha256 digest{};if(!ttr::Sha256Bytes(data,digest))return 1;BCRYPT_ALG_HANDLE algorithm{};BCRYPT_KEY_HANDLE key{};bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0&&BCryptImportKeyPair(algorithm,nullptr,BCRYPT_ECCPRIVATE_BLOB,&key,reinterpret_cast<PUCHAR>(keyBlob.data()),static_cast<ULONG>(keyBlob.size()),0)>=0;ULONG bytes{};if(ok)ok=BCryptSignHash(key,nullptr,reinterpret_cast<PUCHAR>(digest.data()),static_cast<ULONG>(digest.size()),nullptr,0,&bytes,0)>=0;std::vector<UCHAR>signature(bytes);if(ok)ok=BCryptSignHash(key,nullptr,reinterpret_cast<PUCHAR>(digest.data()),static_cast<ULONG>(digest.size()),signature.data(),bytes,&bytes,0)>=0&&Write((std::string(argv[3])+".sig").c_str(),signature.data(),bytes);if(key)BCryptDestroyKey(key);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);return ok?0:1;
  }
  if(argc==5&&std::string(argv[1])=="verify"){
    std::vector<std::byte>keyBlob,data,signature;if(!Read(argv[2],keyBlob)||!Read(argv[3],data)||!Read(argv[4],signature,256))return 1;ttr::Sha256 digest{};if(!ttr::Sha256Bytes(data,digest))return 1;BCRYPT_ALG_HANDLE algorithm{};BCRYPT_KEY_HANDLE key{};bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)>=0&&BCryptImportKeyPair(algorithm,nullptr,BCRYPT_ECCPUBLIC_BLOB,&key,reinterpret_cast<PUCHAR>(keyBlob.data()),static_cast<ULONG>(keyBlob.size()),0)>=0&&BCryptVerifySignature(key,nullptr,reinterpret_cast<PUCHAR>(digest.data()),static_cast<ULONG>(digest.size()),reinterpret_cast<PUCHAR>(signature.data()),static_cast<ULONG>(signature.size()),0)>=0;if(key)BCryptDestroyKey(key);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);return ok?0:1;
  }
  std::cerr<<"usage:\n  manifestsign generate <private.blob> <public.blob>\n  manifestsign sign <private.blob> <compat.bin>\n  manifestsign verify <public.blob> <compat.bin> <compat.sig>\n";return 2;
}
