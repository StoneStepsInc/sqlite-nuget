#include <sqlite3.h>

#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <memory>
#include <array>
#include <format>

using namespace std::literals::string_view_literals;

template <typename T>
struct sqlite_deleter {
   void operator ()(T *ptr)
   {
      sqlite3_free(ptr);
   }
};

int count_results(void *ptr, int ncol, char **values, char **names)
{
   (*reinterpret_cast<int*>(ptr))++;

   return 0;
}

void create_table(sqlite3 *ppDb, const char *table_name, const char *create_table_sql)
{
   char *errmsg = nullptr;

   if(sqlite3_exec(ppDb, std::format("DROP TABLE IF EXISTS {:s};", table_name).c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK)
      throw std::runtime_error(std::unique_ptr<char, sqlite_deleter<char>>(errmsg).get());

   if(sqlite3_exec(ppDb, std::vformat(create_table_sql, std::make_format_args(table_name)).c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK)
      throw std::runtime_error(std::unique_ptr<char, sqlite_deleter<char>>(errmsg).get());
}

int main(void)
{
   printf("SQLite version: %s\n", SQLITE_VERSION);

   printf("SQLite default threading: %s\n", sqlite3_threadsafe() == 0 ? "single-threaded" :
                                        sqlite3_threadsafe() == 2 ? "multi-threaded" : "serialized");

   if(SQLITE_VERSION_NUMBER != sqlite3_libversion_number())
      fprintf(stderr, "WARNING: SQLite header has a different version than the library\n");

   sqlite3 *ppDb = nullptr;
   int errcode = SQLITE_OK;

   try {
      char *errmsg = nullptr;

      if(sqlite3_config(SQLITE_CONFIG_MULTITHREAD) != SQLITE_OK)
         throw std::runtime_error("Cannot configure SQLite to operate as multi-threaded");

      printf("Switched SQLite to operate as multi-threaded\n");

      if(sqlite3_initialize() != SQLITE_OK)
         throw std::runtime_error("SQLite cannot be initialized");

      if((errcode = sqlite3_open_v2("sqlite-test.db", &ppDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr)) != SQLITE_OK)
         throw std::runtime_error(sqlite3_errstr(errcode));

      create_table(ppDb, "test_table", "CREATE TABLE {:s} (rnum REAL NULL, inum INTEGER NOT NULL, txt TEXT NOT NULL);");
      create_table(ppDb, "test_table_fts", "CREATE VIRTUAL TABLE {:s} USING fts5(txt, content='test_table', content_rowid='rowid');");

      // insert a couple of records that will match the following criteria
      if(sqlite3_exec(ppDb, "INSERT INTO test_table (rnum, inum, txt) VALUES (7.89, 123, 'abc');", nullptr, nullptr, &errmsg) != SQLITE_OK)
         throw std::runtime_error(std::unique_ptr<char, sqlite_deleter<char>>(errmsg).get());

      if(sqlite3_exec(ppDb, "INSERT INTO test_table (rnum, inum, txt) VALUES (NULL, 456, 'xyz');", nullptr, nullptr, &errmsg) != SQLITE_OK)
         throw std::runtime_error(std::unique_ptr<char, sqlite_deleter<char>>(errmsg).get());

      // build the full text index (can be rebuild, optimize, integrity-check, merge=N)
      if(sqlite3_exec(ppDb, "INSERT INTO test_table_fts(test_table_fts) VALUES('rebuild');", nullptr, nullptr, &errmsg) != SQLITE_OK)
         throw std::runtime_error(std::unique_ptr<char, sqlite_deleter<char>>(errmsg).get());

      // join both tables to test carray and FTS5 in one statement
      std::string_view sql = "SELECT test_table.rowid as id, power(rnum, 2) as p2rnum, test_table.* "
                              "FROM test_table JOIN test_table_fts ON test_table.rowid = test_table_fts.rowid "
                              "WHERE inum IN carray(?) AND test_table_fts MATCH 'xyz OR abc' "
                              "ORDER BY test_table.rowid DESC;"sv;

      sqlite3_stmt *ppStmt = nullptr;

      if((errcode = sqlite3_prepare_v2(ppDb, sql.data(), (int) sql.length()+1, &ppStmt, nullptr)) != SQLITE_OK)
         throw std::runtime_error(sqlite3_errstr(errcode));

      // bind the array of int values above as a carray virtual table against which inum values will be compared (carray must be enabled)
      std::array<int, 3> ints = {123, 456, 789};

      if((errcode = errcode = sqlite3_carray_bind(ppStmt, 1, ints.data(), static_cast<int>(ints.size()), SQLITE_CARRAY_INT32, SQLITE_STATIC)) != SQLITE_OK)
         throw std::runtime_error(sqlite3_errstr(errcode));

      // these functions are only available when column metadata is enabled when building SQLite
      printf("unaliased 1st column: %s.%s.%s\n", sqlite3_column_database_name(ppStmt, 0), sqlite3_column_table_name(ppStmt, 0), sqlite3_column_origin_name(ppStmt, 0));

      printf("test_table:\n");

      while((errcode = sqlite3_step(ppStmt)) == SQLITE_ROW) {
         for(int i = 0; i < sqlite3_column_count(ppStmt); i++) {
            printf("%7s: ", sqlite3_column_name(ppStmt, i));
            switch(sqlite3_column_type(ppStmt, i)) {
               case SQLITE_INTEGER:
                  printf("%6d", sqlite3_column_int(ppStmt, i));
                  break;
               case SQLITE_FLOAT:
                  printf("%6.3f", sqlite3_column_double(ppStmt, i));
                  break;
               case SQLITE3_TEXT:
                  printf("%6s", sqlite3_column_text(ppStmt, i));
                  break;
               case SQLITE_NULL:
                  printf("%6s", "NULL");
                  break;
            }
         }
         printf("\n");
      }

      if(errcode != SQLITE_DONE)
         fprintf(stderr, "Encountered an error while retieving records (%s)\n", sqlite3_errstr(errcode));

      if((errcode = sqlite3_finalize(ppStmt)) != SQLITE_OK)
         fprintf(stderr, "Cannot finalize a prepared statement (%s)\n", sqlite3_errstr(errcode));

      if((errcode = sqlite3_close(ppDb)) != SQLITE_OK)
         fprintf(stderr, "Cannot close the test SQLite database (%s)\n", sqlite3_errstr(errcode));

      if((errcode = sqlite3_shutdown()) != SQLITE_OK)
         fprintf(stderr, "Cannot shut down SQLite (%s)\n", sqlite3_errstr(errcode));

      return EXIT_SUCCESS;
   }
   catch (const std::exception& err) {
      fprintf(stderr, "ERROR: %s\n", err.what());
   }

   if(ppDb) {
      if(sqlite3_close(ppDb) != SQLITE_OK)
         fprintf(stderr, "Failed to close the test SQLite databasen\n");
   }

   return EXIT_FAILURE;
}
