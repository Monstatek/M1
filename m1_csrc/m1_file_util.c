/* See COPYING.txt for license details. */

/*
 * m1_file_util.c
 */

/*************************** I N C L U D E S **********************************/
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "ff_gen_drv.h"
#include "m1_file_util.h"

/*************************** D E F I N E S ************************************/


//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/



/********************* F U N C T I O N   P R O T O T Y P E S ******************/

const char* fu_get_file_extension(const char *filename);
void fu_get_filename_without_ext(const char *path, char *outName, size_t outSize);
const char* fu_get_filename(const char *path);
void fu_get_directory_path(const char *fullPath, char *outDir, size_t dirSize);
int fs_file_exists(const char *path);
int fs_directory_exists(const char *path);
FRESULT fs_directory_ensure(const char *path);
FRESULT fs_get_free_space(uint64_t *pFree);
void fu_path_combine(char *out, size_t outSize, const char *path, const char *file);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief Extracts the file extension from a filename.
 *
 * @param filename The input filename.
 * @return Pointer to the extension string, or NULL on failure.
 */
/*============================================================================*/
const char* fu_get_file_extension(const char *filename)
{
    if (filename == NULL) return NULL;

    const char *dot = strrchr(filename, '.');
    if (dot == NULL || dot == filename) {
        return NULL;
    }

    return dot + 1;
}


/*============================================================================*/
/**
 * @brief Extracts the filename without its extension from a file path.
 *
 * This function removes both the directory path and the file extension.
 * For example:
 *   "C:\\folder\\test.txt"   → "test"
 *   "/home/user/archive.gz" → "archive"
 *   "no_extension"          → "no_extension"
 *
 * @param path     Full file path to process.
 * @param outName  Output buffer for the filename without extension.
 * @param outSize  Size of the output buffer.
 *
 * @note If the path is NULL, outName is NULL, or outSize is zero,
 *       the function does nothing.
 */
/*============================================================================*/
void fu_get_filename_without_ext(const char *path, char *outName, size_t outSize)
{
    if (path == NULL || outName == NULL || outSize == 0)
        return;

    outName[0] = '\0';

    /* -------------------------
       1)
       ------------------------- */
    const char *filename = path;

    const char *slash1 = strrchr(path, '/');
    const char *slash2 = strrchr(path, '\\');  // Windows

    if (slash1 && slash2)
        filename = (slash1 > slash2) ? slash1 + 1 : slash2 + 1;
    else if (slash1)
        filename = slash1 + 1;
    else if (slash2)
        filename = slash2 + 1;

    /* -------------------------
       2)
       ------------------------- */
    const char *dot = strrchr(filename, '.');
    size_t name_len;

    if (dot == NULL || dot == filename) {

        name_len = strlen(filename);
    } else {
        name_len = (size_t)(dot - filename);
    }

    /* -------------------------
       3)
       ------------------------- */
    if (name_len >= outSize) name_len = outSize - 1;

    strncpy(outName, filename, name_len);
    outName[name_len] = '\0';
}


/*============================================================================*/
/**
 * @brief Extracts the filename part from a full file path.
 *
 * This function returns a pointer to the substring after the last path
 * separator ('/' for Unix/Linux or '\\' for Windows). If no separator
 * exists, the entire input string is considered the filename.
 *
 * @param path The full file path.
 * @return Pointer to the filename portion, or NULL if path is NULL.
 */
/*
 * const char *name;
 * name = fu_get_filename("/home/user/data/test.bin");
 * result : "test.bin"
 *
 */
/*============================================================================*/
const char* fu_get_filename(const char *path)
{
    if (path == NULL)
        return NULL;

    const char *slash1 = strrchr(path, '/');   // Linux, Unix
    const char *slash2 = strrchr(path, '\\');  // Windows

    const char *filename = path;

    if (slash1 && slash2)
        filename = (slash1 > slash2) ? slash1 + 1 : slash2 + 1;
    else if (slash1)
        filename = slash1 + 1;
    else if (slash2)
        filename = slash2 + 1;

    return filename;
}


/*============================================================================*/
/**
 * @brief Extracts only the directory path from a full file path.
 *
 * Example:
 *   Input:  "0://rfid/12345678.rfid"
 *   Output: "0://rfid"
 *
 * If no separator is found, the result becomes an empty string.
 *
 * @param fullPath  Full path string.
 * @param outDir    Buffer for directory part.
 * @param dirSize   Size of outDir buffer.
 */
/*============================================================================*/
void fu_get_directory_path(const char *fullPath, char *outDir, size_t dirSize)
{
    if (!fullPath || !outDir || dirSize == 0) {
        if (outDir) outDir[0] = '\0';
        return;
    }

    outDir[0] = '\0';

    const char *slash1 = strrchr(fullPath, '/');
    const char *slash2 = strrchr(fullPath, '\\');

    const char *sep = NULL;
    if (slash1 && slash2)
        sep = (slash1 > slash2) ? slash1 : slash2;
    else if (slash1)
        sep = slash1;
    else
        sep = slash2;

    if (sep == NULL) {
        // 구분자 없음 → 경로만 추출할 수 없음
        return;
    }

    size_t len = (size_t)(sep - fullPath);
    if (len >= dirSize) len = dirSize - 1;

    strncpy(outDir, fullPath, len);
    outDir[len] = '\0';
}


/*============================================================================*/
/**
 * @brief Checks whether the specified path refers to an existing file.
 *
 * This function calls FatFs f_stat() to determine whether the given path exists.
 * If the path exists and is a regular file, it returns 1. If it exists but is a
 * directory, it returns -1. If the path does not exist or an error occurs, 0 is
 * returned.
 *
 * @param path Path of the file to check.
 * @return 1 if file exists, -1 if it is a directory, 0 if not found or error.
 *
 * @code
 * if (fs_file_exists("/data/config.ini") == 1) {
 *     // File exists
 * } else {
 *     // File not found or is a directory
 * }
 * @endcode
 */
/*============================================================================*/
FRESULT fs_path_kind(const char *path, fs_path_kind_t *kind)
{
    if (!kind) return FR_INVALID_PARAMETER;
    *kind = FS_PATH_NONE;
    if (!path) return FR_INVALID_PARAMETER;
    FILINFO fno;
    FRESULT fr = f_stat(path, &fno);
    if (fr == FR_OK)
        *kind = (fno.fattrib & AM_DIR) ? FS_PATH_DIRECTORY : FS_PATH_FILE;
    return fr;
}

int fs_file_exists(const char *path)
{
    fs_path_kind_t kind;
    if (fs_path_kind(path, &kind) != FR_OK) return 0;
    return kind == FS_PATH_FILE ? 1 : -1;
}


/*============================================================================*/
/**
 * @brief Checks whether the specified path refers to an existing directory.
 *
 * This function uses FatFs f_stat() to inspect the path. If the path exists
 * and is a directory, it returns 1. If the path exists but is a file, it
 * returns -1. If the path does not exist or an error occurs, it returns 0.
 *
 * @param path Path to check.
 * @return 1 if directory exists, -1 if a file exists at the path,
 *         0 if not found or error.
 *
 * @code
 * if (fs_directory_exists("/data/logs") == 1) {
 *     // Directory exists
 * }
 * @endcode
 */
/*============================================================================*/
int fs_directory_exists(const char *path)
{
    fs_path_kind_t kind;
    if (fs_path_kind(path, &kind) != FR_OK) return 0;
    return kind == FS_PATH_DIRECTORY ? 1 : -1;
}


/*============================================================================*/
/**
 * @brief Ensures that the specified directory exists.
 *
 * This function checks whether the given path refers to an existing directory
 * using FatFs f_stat(). If the path exists and is a directory, FR_OK is
 * returned. If the path exists but is a file, FR_EXIST is returned to indicate
 * that a directory cannot be created with the same name.
 *
 * If the path does not exist (FR_NO_FILE or FR_NO_PATH), the function attempts
 * to create the directory using f_mkdir(). Any other error from f_stat() is
 * returned unchanged.
 *
 * @param path Directory path to check or create.
 * @return FR_OK on success, FR_EXIST if a file already exists at the path,
 *         or another FatFs error code on failure.
 *
 * @code
 * if (fs_directory_ensure("/data/logs") == FR_OK) {
 *     // Directory is ready for use
 * } else {
 *     // Error occurred or same-name file exists
 * }
 * @endcode
 */
/*============================================================================*/
FRESULT fs_directory_ensure(const char *path)
{
    FILINFO fno;
    FRESULT fr = f_stat(path, &fno);

    /* 이미 존재하는 경우 */
    if (fr == FR_OK) {
        /* 디렉토리면 정상 */
        if (fno.fattrib & AM_DIR)
            return FR_OK;

        /* 파일이 존재하면 디렉토리를 만들 수 없음 */
        return FR_EXIST;
    }

    /* 존재하지 않으면 생성 시도 */
    if (fr == FR_NO_FILE || fr == FR_NO_PATH)
        return f_mkdir(path);

    /* 기타 오류 */
    return fr;
}


/*============================================================================*/
/**
 * @brief Retrieves the available free space on the filesystem.
 *
 * This function calls FatFs f_getfree() to obtain the number of free clusters
 * and the filesystem object. It then calculates the free space in bytes using
 * the cluster size and the sector size (ssize). The result is stored in
 * `pFree` as a 64-bit value, allowing support for large storage devices.
 *
 * @param[out] pFree Pointer to a variable that receives the free space in bytes.
 *
 * @return FR_OK on success, or a FatFs error code if retrieval fails.
 *
 * @code
 * uint64_t free_bytes;
 * if (fs_get_free_space(&free_bytes) == FR_OK) {
 *     printf("Free space: %llu bytes\n", free_bytes);
 * }
 * @endcode
 */
/*============================================================================*/
FRESULT fs_get_free_space(uint64_t *pFree)
{
    FATFS *fs;
    DWORD fre_clust;

    FRESULT fr = f_getfree("", &fre_clust, &fs);
    if (fr != FR_OK) return fr;

    uint64_t free_sectors = (uint64_t)fre_clust * fs->csize;
    uint64_t bytes_per_sector;

    bytes_per_sector = FF_MAX_SS;
#if FF_MAX_SS != FF_MIN_SS
    bytes_per_sector = fs->ssize;
#endif // #if FF_MAX_SS != _MIN_SS

    *pFree = free_sectors * bytes_per_sector;

    return FR_OK;
}


/*============================================================================*/
/**
 * @brief Combines a base path and a filename into a single path string.
 *
 * This function safely joins a directory path and a filename while handling:
 * - Empty or NULL path/file inputs
 * - Absolute file paths (which override the base path)
 * - Platform-dependent path separators ('/' or '\\')
 * - Automatic insertion of a separator between path and filename
 * - Removal of unnecessary leading separators in the filename
 *
 * @param out     Output buffer where the combined path is stored.
 * @param outSize Size of the output buffer.
 * @param path    Base directory path.
 * @param file    File name or relative path to append.
 *
 * @note If both @p path and @p file are empty or NULL, @p out remains empty.
 *       If @p file is an absolute path, it replaces @p path entirely.
 *
 ** @code
 * char buf[256];
 *
 * // Example 1: Normal combination
 * fu_path_combine(buf, sizeof(buf), "C:/data", "test.txt");
 * // Result: "C:/data/test.txt"
 *
 * // Example 2: Avoid duplicate separators
 * fu_path_combine(buf, sizeof(buf), "C:/data/", "test.txt");
 * // Result: "C:/data/test.txt"
 *
 * // Example 3: Absolute file overrides path
 * fu_path_combine(buf, sizeof(buf), "C:/data", "/logs/output.txt");
 * // Result: "/logs/output.txt"
 *
 * // Example 4: Only filename given
 * fu_path_combine(buf, sizeof(buf), NULL, "readme.md");
 * // Result: "readme.md"
 *
 * // Example 5: Only base path given
 * fu_path_combine(buf, sizeof(buf), "/usr/local/bin", NULL);
 * // Result: "/usr/local/bin"
 * @endcode
 */
/*============================================================================*/
#ifdef _WIN32
    #define PATH_SEP '\\'   // Windows 경로 구분??
#else
    #define PATH_SEP '/'    // Linux/Unix/macOS 경로 구분??
#endif

void fu_path_combine(char *out, size_t outSize, const char *path, const char *file)
{
    if (out == NULL || outSize == 0) {
        return;
    }

    out[0] = '\0';

    if ((path == NULL || path[0] == '\0') &&
        (file == NULL || file[0] == '\0')) {
        return;
    }

    if (path == NULL || path[0] == '\0') {
        if (file)
            snprintf(out, outSize, "%s", file);
        return;
    }

    if (file == NULL || file[0] == '\0') {
        snprintf(out, outSize, "%s", path);
        return;
    }


    int file_is_absolute = 0;

#ifdef _WIN32

    if (file[0] == '\\' || file[0] == '/') {
        file_is_absolute = 1;
    } else if (isalpha((unsigned char)file[0]) && file[1] == ':') {
        file_is_absolute = 1;
    }
#else
    if (file[0] == '/') {
        file_is_absolute = 1;
    }
#endif

    if (file_is_absolute) {
        snprintf(out, outSize, "%s", file);
        return;
    }

    snprintf(out, outSize, "%s", path);
    size_t len = strlen(out);

    if (len > 0) {
        char last = out[len - 1];
        if (last != '/' && last != '\\') {
            if (len + 1 < outSize) {
                out[len] = PATH_SEP;
                out[len + 1] = '\0';
                len++;
            }
        }
    }

    const char *fname = file;
    if (fname[0] == '/' || fname[0] == '\\') {
        fname++;
    }

    strncat(out, fname, outSize - strlen(out) - 1);
}
