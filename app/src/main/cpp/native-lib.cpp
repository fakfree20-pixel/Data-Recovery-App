#include <jni.h>
#include <string>
#include <vector>
#include <fstream>
#include <android/log.h>

#define LOG_TAG "NativeCarver"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct CarvedResult {
    std::string fileType;
    long offset;
    long estimatedSize;
};

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_example_MainActivity_nativeScanFile(
        JNIEnv* env,
        jobject /* this */,
        jstring filePathStr) {

    const char* filePath = env->GetStringUTFChars(filePathStr, nullptr);
    if (!filePath) {
        return nullptr;
    }

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOGE("Failed to open file for scanning: %s", filePath);
        env->ReleaseStringUTFChars(filePathStr, filePath);
        return nullptr;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    LOGI("Starting C++ deep carver scan on file: %s (size: %ld bytes)", filePath, (long)fileSize);

    const size_t CHUNK_SIZE = 65536; // 64KB buffer
    std::vector<char> buffer(CHUNK_SIZE);
    std::vector<CarvedResult> results;

    long currentOffset = 0;
    std::vector<char> prevChunkTail;

    while (file.read(buffer.data(), CHUNK_SIZE) || file.gcount() > 0) {
        std::streamsize bytesRead = file.gcount();
        
        std::vector<char> searchBlock;
        searchBlock.reserve(prevChunkTail.size() + bytesRead);
        searchBlock.insert(searchBlock.end(), prevChunkTail.begin(), prevChunkTail.end());
        searchBlock.insert(searchBlock.end(), buffer.data(), buffer.data() + bytesRead);

        long blockBaseOffset = currentOffset - prevChunkTail.size();

        for (size_t i = 0; i + 16 < searchBlock.size(); ++i) {
            unsigned char b0 = searchBlock[i];
            unsigned char b1 = searchBlock[i+1];
            unsigned char b2 = searchBlock[i+2];
            unsigned char b3 = searchBlock[i+3];

            // 1. JPEG Magic Bytes: 0xFF 0xD8 0xFF
            if (b0 == 0xFF && b1 == 0xD8 && b2 == 0xFF) {
                long foundOffset = blockBaseOffset + i;
                results.push_back({"JPEG", foundOffset, 1024 * 500});
                LOGI("Carved JPEG at offset: %ld", foundOffset);
            }

            // 2. MP4 Video (ftyp box at offset +4)
            if (i + 8 < searchBlock.size()) {
                if (searchBlock[i+4] == 'f' && searchBlock[i+5] == 't' &&
                    searchBlock[i+6] == 'y' && searchBlock[i+7] == 'p') {
                    long foundOffset = blockBaseOffset + i;
                    results.push_back({"MP4", foundOffset, 1024 * 1024 * 5});
                    LOGI("Carved MP4 at offset: %ld", foundOffset);
                }
            }

            // 3. SQLite Database Header: "SQLite format 3\0" (16 bytes)
            if (i + 15 < searchBlock.size()) {
                if (b0 == 'S' && b1 == 'Q' && b2 == 'L' && b3 == 'i' &&
                    searchBlock[i+4] == 't' && searchBlock[i+5] == 'e' &&
                    searchBlock[i+6] == ' ' && searchBlock[i+7] == 'f' &&
                    searchBlock[i+8] == 'o' && searchBlock[i+9] == 'r' &&
                    searchBlock[i+10] == 'm' && searchBlock[i+11] == 'a' &&
                    searchBlock[i+12] == 't' && searchBlock[i+13] == ' ' &&
                    searchBlock[i+14] == '3' && searchBlock[i+15] == 0x00) {
                    long foundOffset = blockBaseOffset + i;
                    results.push_back({"SQLite", foundOffset, 1024 * 1024});
                    LOGI("Carved SQLite DB at offset: %ld", foundOffset);
                }
            }
        }

        size_t tailSize = (bytesRead > 32) ? 32 : bytesRead;
        prevChunkTail.assign(searchBlock.end() - tailSize, searchBlock.end());

        currentOffset += bytesRead;
        if (file.eof()) break;
    }

    file.close();
    env->ReleaseStringUTFChars(filePathStr, filePath);

    jclass resultClass = env->FindClass("java/lang/String");
    jobjectArray jResults = env->NewObjectArray(results.size(), resultClass, nullptr);

    for (size_t k = 0; k < results.size(); ++k) {
        std::string info = results[k].fileType + "|" + std::to_string(results[k].offset) + "|" + std::to_string(results[k].estimatedSize);
        jstring jInfo = env->NewStringUTF(info.c_str());
        env->SetObjectArrayElement(jResults, k, jInfo);
        env->DeleteLocalRef(jInfo);
    }

    return jResults;
}
