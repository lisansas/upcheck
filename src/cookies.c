#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <shlwapi.h>
#include <windows.h>
#include "spinlock.h"
#include "sqlite3.h"
#include "ini_parser.h"

#define LDOWRD 8
#define LINE_SIZE 2048
#define CHECK_RC(rc, szinfo, err, db)      \
    if (rc != SQLITE_OK)                   \
    {                                      \
        printf("%s error!\n", szinfo);     \
        printf("%s\n", err);               \
        sqlite3_free(err);                 \
        return 0;                          \
    }

static char baidu_login[MAX_PATH + 1];
static char baidu_set[NAMES_LEN + 1];

int
parse_baidu_cookies(char *cookies, int len)
{
    if (strlen(baidu_login) < 2 || strlen(baidu_set) < 2)
    {
        printf("parse_baidu_cookies return false\n");
        return -1;
    }
    _snprintf(cookies, len - 1, "%s; %s", baidu_login, baidu_set);
    return 0;
}

static int
parse_sqlite_cookies(void *hfile, int count, char **column, char **names)
{
    char only[LDOWRD] = { 0 };
    char secure[LDOWRD] = { 0 };
    char expir[MD5_LEN] = { 0 };
    char path[MD5_LEN] = { 0 };
    char tmp_t[VALUE_LEN + 1] = { 0 };
    char host[VALUE_LEN + 1] = { 0 };
    char name[VALUE_LEN + 1] = { 0 };
    char *value = NULL;
    char line[LINE_SIZE + 1] = { 0 };
    for (int i = 0; i < count; i++)
    {
        if (!_stricmp(names[i], "host"))
            strncpy(tmp_t, column[i], VALUE_LEN);
        if (!_stricmp(names[i], "name"))
            strncpy(name, column[i], VALUE_LEN);
        if (!_stricmp(names[i], "value"))
            value = _strdup(column[i]);
        if (!_stricmp(names[i], "path"))
            strncpy(path, column[i], MD5_LEN);
        if (!_stricmp(names[i], "expiry"))
            strncpy(expir, column[i], MD5_LEN);
        if (!_stricmp(names[i], "isSecure"))
            strncpy(secure, column[i], LDOWRD);
        if (!_stricmp(names[i], "isHttpOnly"))
            strncpy(only, column[i], LDOWRD);
    }
    if (atoi(only))
    {
        _snprintf(host, VALUE_LEN, "#HttpOnly_%s", tmp_t);
        _snprintf(only, LDOWRD, "TRUE");
    }
    else
    {
        _snprintf(host, VALUE_LEN, "%s", tmp_t);
        _snprintf(only, LDOWRD, "FALSE");
    }
    if (!atoi(secure))
    {
        _snprintf(secure, LDOWRD, "FALSE");
    }
    else
    {
        _snprintf(secure, LDOWRD, "TRUE");
    }
    {
        LPCSTR host1 = "#HttpOnly_.baidu.com";
        LPCSTR host2 = "#HttpOnly_.pcs.baidu.com";
        LPCSTR key1 = "BDUSS";
        LPCSTR key2 = "pcsett";
        if (strcmp(host, host1) == 0 && strcmp(name, key1) == 0)
        {
            _snprintf(baidu_login, MAX_PATH, "%s=%s", key1, value);
        }
        if (strcmp(host, host2) == 0 && strcmp(name, key2) == 0)
        {
            _snprintf(baidu_set, NAMES_LEN, "%s=%s", key2, value);
        }
    }
    _snprintf(line, LINE_SIZE, "%s\t%s\t%s\t%s\t%s\t%s\t%s\n", host, only, path, secure, expir, name, value);
    if (value)
    {
        free(value);
    }
    DWORD written;
    WriteFile(hfile, line, (DWORD)strlen(line), &written, NULL);
    return 0;
}

static int
sqlite_txt(LPCWSTR cookie_path, HANDLE hfile)
{
    sqlite3 *db;
    char *err = NULL;
    char utf8[MAX_PATH + 2];
    int rc = -1;
    if (!WideCharToMultiByte(CP_UTF8, 0, cookie_path, -1, utf8, sizeof(utf8), NULL, NULL))
    {
        printf("WideCharToMultiByte cookie_path error: %lu\n", GetLastError());
        return -1;
    }
    if ((rc = sqlite3_open(utf8, &db)) != SQLITE_OK)
    {
        printf("sqlite3_open false\n");
        return -1;
    }
    rc = sqlite3_exec(db, "SELECT host,isHttpOnly,path,isSecure,expiry,name,value FROM moz_cookies;", parse_sqlite_cookies, (void *) hfile, &err);
    if (rc != SQLITE_OK)
    {
        printf("select table error : %s\n", err);
        if (err)
        {
            sqlite3_free(err);
        }
    }
    sqlite3_close(db);
    return rc;
}

int
dump_cookies(const wchar_t *sql_path)
{
    DWORD written = 0;
    wchar_t temp_path[MAX_PATH];
    wchar_t cookies[MAX_PATH];
    LPCSTR notes = "# Netscape HTTP Cookie File\n"
                   "# select host,isHttpOnly,path,isSecure,expiry,name,value from moz_cookies\n\n";
    if (sql_path == NULL || !PathFileExistsW(sql_path))
    {
        printf("%S no exist\n", sql_path);
        return -1;
    }
    if (!GetTempPathW(MAX_PATH, temp_path))
    {
        return -1;
    }
    if (!GetTempFileNameW(temp_path, L"cke", 0, cookies))
    {
        printf("GetTempFileNameW return false\n");
        return -1;
    }
    file_info.cookie_handle =
        CreateFileW(cookies, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (INVALID_HANDLE_VALUE == file_info.cookie_handle)
    {
        printf("CreateFileW false\n");
        file_info.cookie_handle = NULL;
        return -1;
    }
    WriteFile(file_info.cookie_handle, notes, (DWORD)strlen(notes), &written, NULL);
    if (sqlite_txt(sql_path, file_info.cookie_handle) != SQLITE_OK)
    {
        CloseHandle(file_info.cookie_handle);
        file_info.cookie_handle = NULL;
    }
    else
    {
        FlushFileBuffers(file_info.cookie_handle);
        ini_make_u8(cookies, file_info.cookies, MAX_PATH - 1);
    }
    return 0;
}

static int
back_downloaded(int64_t *psize, int count, char **column, char **names)
{
    *psize = _atoi64(column[0]);
    return 0;
}

bool
get_down_size(int64_t *psize)
{
    int  rc = 0;
    char *msg = NULL;
    const char *m_sql = "select max(szTotal) AS szTotal from download_info;";
    if (NULL == file_info.sql)
    {
        return 0;
    }
    rc = sqlite3_exec(file_info.sql, m_sql, back_downloaded, psize, &msg);
    CHECK_RC(rc, "get szTotal error", msg, file_info.sql);
    return true;
}

int
get_ranges(sql_node *node)
{
    int  rc = 0;
    int  num = 0;
    uint32_t szThread;
    int64_t  szBegin;
    int64_t  szEnd;
    int64_t  szDown;
    int      szStatus;
    int  col = 0;
    sqlite3_stmt *pstmt;
    char *msg = NULL;
    const char *tail;
    const char *m_sql = "select szBegin,szEnd,szDown,szThread,szStatus from download_info;";
    if (NULL == file_info.sql)
    {
        return 0;
    }
    rc = sqlite3_prepare_v2(file_info.sql,m_sql,(int)strlen(m_sql),&pstmt,&tail);
    if (rc != SQLITE_OK)
    {
        printf("sqlite3_exec error in get_ranges(): %s\n", msg);
        sqlite3_close(file_info.sql);
        return 0;
    }
    while(sqlite3_step(pstmt) == SQLITE_ROW)
    {
        col = 0;
        szBegin = sqlite3_column_int64(pstmt,col++);
        szEnd = sqlite3_column_int64(pstmt,col++);
        szDown = sqlite3_column_int64(pstmt,col++);
        szThread = (uint32_t)sqlite3_column_int64(pstmt,col++);
        szStatus = sqlite3_column_int(pstmt,col++);

        if (!szStatus)
        {
            node[num].startidx = szBegin;
            node[num].endidx = szEnd;
            node[num].szdown = szDown;
            node[num].thread = szThread;
            num++;
        }
    }
    sqlite3_finalize(pstmt);
    return num;
}

bool
update_status(uint32_t thread, int status)
{
    int  rc = 0;
    char *msg = NULL;
    char m_sql[VALUE_LEN] = {0};
    if (NULL == file_info.sql)
    {
        return false;
    }
    _snprintf(m_sql, VALUE_LEN - 1, "update download_info set szStatus=%d where szThread=%u;" ,status, thread);
    rc = sqlite3_exec(file_info.sql, m_sql, 0, 0, &msg);
    CHECK_RC(rc, "update_status error", msg, file_info.sql);
    return true;
}

bool
update_ranges(uint32_t thread, int64_t begin, int64_t size)
{
    int  rc = 0;
    char *msg = NULL;
    char m_sql[VALUE_LEN] = {0};
    if (NULL == file_info.sql)
    {
        return false;
    }
    rc = sqlite3_exec(file_info.sql, "PRAGMA journal_mode=OFF;", 0, 0, &msg);
    CHECK_RC(rc, "journal_mode=OFF error", msg, file_info.sql);
    _snprintf(m_sql, VALUE_LEN - 1, "update download_info set szDown=%I64d,szTotal=%I64d where szThread=%u;" ,begin, size, thread);
    rc = sqlite3_exec(file_info.sql, m_sql, 0, 0, &msg);
    CHECK_RC(rc, "update ranges error", msg, file_info.sql);
    return true;
}

bool
thread_insert(const char *url, int64_t begin, int64_t end, int64_t down, int64_t total, uint32_t thread, uint32_t pid, int status)
{
    int  rc = 0;
    char *msg = NULL;
    char m_sql[COOKE_LEN] = {0};
    if (NULL == file_info.sql)
    {
        return false;
    }
    _snprintf(m_sql, COOKE_LEN - 1, "insert into download_info(szUrl,szBegin,szEnd,szDown,szTotal,szThread,szPid,szStatus) values('%s',%I64d,%I64d,%I64d,%I64d,%u,%u,%d);"
              ,url, begin, end, down, total, thread, pid, status);
    rc = sqlite3_exec(file_info.sql, m_sql, 0, 0, &msg);
    CHECK_RC(rc, "thread_insert error", msg, file_info.sql);
    return true;
}

void
clean_sql_logs(void)
{
    if (file_info.sql != NULL)
    {
        sqlite3_close(file_info.sql);
    }
}

bool
init_sql_logs(const wchar_t *logs)
{
    int   rc = 0;
    char  *msg = NULL;
    const char *sql = "create table download_info(szId INTEGER PRIMARY KEY, szUrl char, szBegin BIGINT, szEnd BIGINT, szDown BIGINT, "
                      "szTotal BIGINT, szThread INT UNSIGNED, szPid INT UNSIGNED, szStatus int);";
    char  utf8[MAX_PATH + 2];
    sqlite3 *db = NULL;
    if (!WideCharToMultiByte(CP_UTF8, 0, logs, -1, utf8, sizeof(utf8), NULL, NULL))
    {
        printf("WideCharToMultiByte %S error: %lu\n", logs, GetLastError());
        return false;
    }
    rc = sqlite3_open_v2(utf8, &db, SQLITE_OPEN_READWRITE, NULL);
    if (rc == SQLITE_CANTOPEN)
    {
        char m_sql[MAX_PATH] = {0};
        rc = sqlite3_open_v2(utf8, &db, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE, NULL);
        if (rc != SQLITE_OK)
        {
            printf("sqlite3_open_v2[%s] error: %d\n", utf8, rc);
            return false;
        }
        rc = sqlite3_exec(db, sql, 0, 0, &msg);
        if (rc != SQLITE_OK)
        {
            printf("create table error: %s\n", msg);
            sqlite3_free(msg);
            sqlite3_close(db);
            return false;
        }
    }
    else if (rc != SQLITE_OK)
    {
        printf("sqlite3_open_v2 error: %d\n", rc);
        return false;
    }
    file_info.sql = db;
    return true;
}
