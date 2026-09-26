#pragma once

#include <jni.h>
#include <string>
#include <vector>

bool CheckAndLogJavaException(JNIEnv* env, const wchar_t* stage);

bool CheckAndLogJavaMainException(JNIEnv* env, const wchar_t* stage);

// Forge 1.12.2 installs FMLSecurityManager, which deliberately converts System.exit
// into ExitTrappedException so the embedded JVM host can retain control of the process.
bool IsForgeLegacyExitTrappedException(JNIEnv* env);

bool LaunchInvokeJavaMain(JNIEnv* env, const std::wstring& className, const std::vector<std::string>& args);
void LaunchSetJavaSystemProperty(JNIEnv* env, const std::wstring& key, const std::wstring& value);
