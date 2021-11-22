/*
 * Copyright 2020 OmniSci, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file ShowCommandsDdlTest.cpp
 * @brief Test suite for SHOW DDL commands
 */

#include <gtest/gtest.h>
#include "boost/filesystem.hpp"

#include "DBHandlerTestHelpers.h"
#include "DataMgr/BufferMgr/CpuBufferMgr/CpuBufferMgr.h"
#include "DataMgr/BufferMgr/GpuCudaBufferMgr/GpuCudaBufferMgr.h"
#include "Shared/File.h"
#include "TestHelpers.h"

#ifndef BASE_PATH
#define BASE_PATH "./tmp"
#endif

extern bool g_enable_fsi;
extern bool g_enable_system_tables;

class ShowUserSessionsTest : public DBHandlerTestFixture {
 public:
  void SetUp() override {
    DBHandlerTestFixture::SetUp();
    // Default connection string outside of thrift
    connection_string = "tcp:";
    // Check that default only user session exists
    TQueryResult result;
    sql(result, "SHOW USER SESSIONS;");
    assertExpectedFormat(result);
    assertNumSessions(result, 1);
    assertSessionResultFound(result, "admin", "omnisci", 1);
    getID(result, "admin", "omnisci", admin_id);
  }

  static void SetUpTestSuite() {
    createDBHandler();
    users_ = {"user1", "user2"};
    superusers_ = {"super1", "super2"};
    dbs_ = {"db1", "db2"};
    createDBs();
    createUsers();
    createSuperUsers();
  }

  static void TearDownTestSuite() {
    dropUsers();
    dropSuperUsers();
    dropDBs();
  }

  void TearDown() override {
    // Check that default only user session still exists
    TQueryResult result;
    sql(result, "SHOW USER SESSIONS;");
    assertExpectedFormat(result);
    assertNumSessions(result, 1);
    assertSessionResultFound(result, "admin", "omnisci", admin_id);
    DBHandlerTestFixture::TearDown();
  }

  static void createUsers() {
    for (const auto& user : users_) {
      std::stringstream create;
      create << "CREATE USER " << user
             << " (password = 'HyperInteractive', is_super = 'false', "
                "default_db='omnisci');";
      sql(create.str());
      for (const auto& db : dbs_) {
        std::stringstream grant;
        grant << "GRANT ALL ON DATABASE  " << db << " to " << user << ";";
        sql(grant.str());
      }
    }
  }

  static void createSuperUsers() {
    for (const auto& user : superusers_) {
      std::stringstream create;
      create
          << "CREATE USER " << user
          << " (password = 'HyperInteractive', is_super = 'true', default_db='omnisci');";
      sql(create.str());
      for (const auto& db : dbs_) {
        std::stringstream grant;
        grant << "GRANT ALL ON DATABASE  " << db << " to " << user << ";";
        sql(grant.str());
      }
    }
  }

  static void dropUsers() {
    for (const auto& user : users_) {
      std::stringstream drop;
      drop << "DROP USER " << user << ";";
      sql(drop.str());
    }
  }

  static void dropSuperUsers() {
    for (const auto& user : superusers_) {
      std::stringstream drop;
      drop << "DROP USER " << user << ";";
      sql(drop.str());
    }
  }

  static void createDBs() {
    for (const auto& db : dbs_) {
      std::stringstream create;
      create << "CREATE DATABASE " << db << " (owner = 'admin');";
      sql(create.str());
    }
  }

  static void dropDBs() {
    for (const auto& db : dbs_) {
      std::stringstream drop;
      drop << "DROP DATABASE " << db << ";";
      sql(drop.str());
    }
  }

  enum ColumnIndex { ID, USERNAME, CONNECTION_INFO, DB_NAME };

  void assertExpectedFormat(const TQueryResult& result) {
    ASSERT_EQ(result.row_set.is_columnar, true);
    ASSERT_EQ(result.row_set.columns.size(), size_t(4));
    ASSERT_EQ(result.row_set.row_desc[ID].col_type.type, TDatumType::STR);
    ASSERT_EQ(result.row_set.row_desc[ID].col_name, "session_id");
    ASSERT_EQ(result.row_set.row_desc[USERNAME].col_type.type, TDatumType::STR);
    ASSERT_EQ(result.row_set.row_desc[USERNAME].col_name, "login_name");
    ASSERT_EQ(result.row_set.row_desc[CONNECTION_INFO].col_type.type, TDatumType::STR);
    ASSERT_EQ(result.row_set.row_desc[CONNECTION_INFO].col_name, "client_address");
    ASSERT_EQ(result.row_set.row_desc[DB_NAME].col_type.type, TDatumType::STR);
    ASSERT_EQ(result.row_set.row_desc[DB_NAME].col_name, "db_name");
  }

  // assert expected_matches results with username, database,
  void assertSessionResultFound(const TQueryResult& result,
                                const std::string& username,
                                const std::string& db,
                                int expected_matches) {
    int num_matches = 0;
    for (size_t i = 0; i < result.row_set.columns[ID].data.str_col.size(); ++i) {
      if (result.row_set.columns[USERNAME].data.str_col[i] == username &&
          result.row_set.columns[DB_NAME].data.str_col[i] == db) {
        num_matches++;
      }
    }
    ASSERT_EQ(num_matches, expected_matches);
  }

  // assert one result with username, database, and ID found
  void assertSessionResultFound(const TQueryResult& result,
                                const std::string& username,
                                const std::string& db,
                                const std::string& id) {
    int num_matches = 0;
    for (size_t i = 0; i < result.row_set.columns[ID].data.str_col.size(); ++i) {
      if (result.row_set.columns[USERNAME].data.str_col[i] == username &&
          result.row_set.columns[DB_NAME].data.str_col[i] == db &&
          result.row_set.columns[ID].data.str_col[i] == id &&
          result.row_set.columns[CONNECTION_INFO].data.str_col[i] == connection_string) {
        num_matches++;
      }
    }
    ASSERT_EQ(num_matches, 1);
  }

  // Get ID of unique session with username and database
  void getID(const TQueryResult& result,
             const std::string& username,
             const std::string& db,
             std::string& retval) {
    for (size_t i = 0; i < result.row_set.columns[ID].data.str_col.size(); ++i) {
      if (result.row_set.columns[USERNAME].data.str_col[i] == username &&
          result.row_set.columns[DB_NAME].data.str_col[i] == db &&
          result.row_set.columns[CONNECTION_INFO].data.str_col[i] == connection_string) {
        // Only one match should be found
        ASSERT_EQ(retval.length(), size_t(0));
        retval = result.row_set.columns[ID].data.str_col[i];
        ASSERT_GT(retval.length(), size_t(0));
      }
    }
    ASSERT_GT(retval.length(), size_t(0));
  }

  void assertNumSessions(const TQueryResult& result, size_t num_session) {
    ASSERT_EQ(num_session, result.row_set.columns[ID].data.str_col.size());
  }
  std::vector<std::string> get_users() { return users_; }
  std::vector<std::string> get_superusers() { return superusers_; }
  std::vector<std::string> get_dbs() { return dbs_; }

 private:
  static std::vector<std::string> users_;
  static std::vector<std::string> superusers_;
  static std::vector<std::string> dbs_;

  std::string admin_id;
  std::string connection_string;
};

std::vector<std::string> ShowUserSessionsTest::users_;
std::vector<std::string> ShowUserSessionsTest::superusers_;
std::vector<std::string> ShowUserSessionsTest::dbs_;

TEST_F(ShowUserSessionsTest, SHOW) {
  // check default admin session is created
  TQueryResult result;
  sql(result, "SHOW USER SESSIONS;");
  assertExpectedFormat(result);
  assertNumSessions(result, 1);
  assertSessionResultFound(result, "admin", "omnisci", 1);
}

TEST_F(ShowUserSessionsTest, SHOW_ADMIN_MULTIDB) {
  TSessionId new_session;
  login("admin", "HyperInteractive", "db1", new_session);
  TQueryResult result;
  sql(result, "SHOW USER SESSIONS;");
  assertExpectedFormat(result);
  assertNumSessions(result, 2);
  assertSessionResultFound(result, "admin", "db1", 1);
  assertSessionResultFound(result, "admin", "omnisci", 1);
  logout(new_session);
}

TEST_F(ShowUserSessionsTest, SHOW_ADMIN_MULTISESSION_SINGLEDB) {
  TSessionId new_session;
  login("admin", "HyperInteractive", "omnisci", new_session);
  TQueryResult result;
  std::string query{"SHOW USER SESSIONS;"};
  sql(result, query);
  assertExpectedFormat(result);
  assertNumSessions(result, 2);
  assertSessionResultFound(result, "admin", "omnisci", 2);
  logout(new_session);
}

TEST_F(ShowUserSessionsTest, SHOW_USERS_MULTISESSION) {
  TSessionId session1;
  login("user1", "HyperInteractive", "db1", session1);
  TSessionId session2;
  login("user2", "HyperInteractive", "db1", session2);
  TQueryResult result;
  std::string query{"SHOW USER SESSIONS;"};
  sql(result, query);
  assertExpectedFormat(result);
  assertNumSessions(result, 3);
  assertSessionResultFound(result, "admin", "omnisci", 1);
  assertSessionResultFound(result, "user1", "db1", 1);
  assertSessionResultFound(result, "user2", "db1", 1);
  logout(session1);
  logout(session2);
}

TEST_F(ShowUserSessionsTest, SHOW_USERS_MULTIDBS) {
  TSessionId session1;
  login("user1", "HyperInteractive", "db1", session1);
  TSessionId session2;
  login("user2", "HyperInteractive", "db2", session2);
  TQueryResult result;
  std::string query{"SHOW USER SESSIONS;"};
  sql(result, query);
  assertExpectedFormat(result);
  assertNumSessions(result, 3);
  assertSessionResultFound(result, "admin", "omnisci", 1);
  assertSessionResultFound(result, "user1", "db1", 1);
  assertSessionResultFound(result, "user2", "db2", 1);
  logout(session1);
  logout(session2);
}

TEST_F(ShowUserSessionsTest, SHOW_USERS_ALL) {
  std::vector<TSessionId> session_ids;
  for (int copies = 1; copies < 4; copies++) {
    for (auto const& user : get_users()) {
      for (auto const& db : get_dbs()) {
        TSessionId session;
        login(user, "HyperInteractive", db, session);
        session_ids.push_back(session);
      }
    }

    TQueryResult result;
    sql(result, "SHOW USER SESSIONS;");
    assertExpectedFormat(result);
    assertNumSessions(result, 1 + (copies * get_users().size() * get_dbs().size()));
    for (auto const& user : get_users()) {
      for (auto const& db : get_dbs()) {
        assertSessionResultFound(result, user, db, copies);
      }
    }
  }

  for (auto const& session_id : session_ids) {
    logout(session_id);
  }
}

TEST_F(ShowUserSessionsTest, SHOW_USERS_MULTIDB_LOGOUT) {
  TSessionId session1;
  login("user1", "HyperInteractive", "db1", session1);
  TSessionId session2;
  login("user2", "HyperInteractive", "db2", session2);
  std::string session2_id;
  {
    TQueryResult result;
    sql(result, "SHOW USER SESSIONS;");
    assertExpectedFormat(result);
    assertNumSessions(result, 3);
    assertSessionResultFound(result, "admin", "omnisci", 1);
    assertSessionResultFound(result, "user1", "db1", 1);
    assertSessionResultFound(result, "user2", "db2", 1);
    getID(result, "user2", "db2", session2_id);
  }

  logout(session1);
  {
    TQueryResult result;
    sql(result, "SHOW USER SESSIONS;");
    assertExpectedFormat(result);
    assertNumSessions(result, 2);
    assertSessionResultFound(result, "admin", "omnisci", 1);
    assertSessionResultFound(result, "user2", "db2", session2_id);
  }

  logout(session2);
  {
    TQueryResult result;
    sql(result, "SHOW USER SESSIONS;");
    assertExpectedFormat(result);
    assertNumSessions(result, 1);
    assertSessionResultFound(result, "admin", "omnisci", 1);
  }
}

TEST_F(ShowUserSessionsTest, PRIVILEGES_SUPERUSER) {
  TSessionId supersession;
  login("super1", "HyperInteractive", "db2", supersession);
  {
    TQueryResult result;
    std::string query{"SHOW USER SESSIONS;"};
    sql(result, query, supersession);
    assertExpectedFormat(result);
    assertNumSessions(result, 2);
    assertSessionResultFound(result, "admin", "omnisci", 1);
    assertSessionResultFound(result, "super1", "db2", 1);
  }
  logout(supersession);
}

TEST_F(ShowUserSessionsTest, PRIVILEGES_NONSUPERUSER) {
  TSessionId usersession;
  login("user1", "HyperInteractive", "db1", usersession);

  try {
    TQueryResult result;
    std::string query{"SHOW USER SESSIONS;"};
    sql(result, query, usersession);
    FAIL() << "An exception should have been thrown for this test case.";
  } catch (const TOmniSciException& e) {
    ASSERT_EQ(
        "SHOW USER SESSIONS failed, because it can only be executed by super "
        "user.",
        e.error_msg);
  }

  logout(usersession);
}

class ShowUserDetailsTest : public DBHandlerTestFixture {
 public:
  void SetUp() override { DBHandlerTestFixture::SetUp(); }

  static void SetUpTestSuite() {
    createDBHandler();
    users_ = {"user1", "user2"};
    superusers_ = {"super1", "super2"};
    dbs_ = {"omnisci"};
    createUsers();
    createSuperUsers();
  }

  static void TearDownTestSuite() {
    dropUsers();
    dropSuperUsers();
  }

  void TearDown() override { DBHandlerTestFixture::TearDown(); }

  static void createUsers() {
    for (const auto& user : users_) {
      std::stringstream create;
      create << "CREATE USER " << user
             << " (password = 'HyperInteractive', is_super = 'false', "
                "default_db='omnisci');";
      sql(create.str());
      for (const auto& db : dbs_) {
        std::stringstream grant;
        grant << "GRANT ALL ON DATABASE  " << db << " to " << user << ";";
        sql(grant.str());
      }
    }
  }

  static void createSuperUsers() {
    for (const auto& user : superusers_) {
      std::stringstream create;
      create
          << "CREATE USER " << user
          << " (password = 'HyperInteractive', is_super = 'true', default_db='omnisci');";
      sql(create.str());
      for (const auto& db : dbs_) {
        std::stringstream grant;
        grant << "GRANT ALL ON DATABASE  " << db << " to " << user << ";";
        sql(grant.str());
      }
    }
  }

  static void dropUsers() {
    for (const auto& user : users_) {
      std::stringstream drop;
      drop << "DROP USER " << user << ";";
      sql(drop.str());
    }
  }

  static void dropSuperUsers() {
    for (const auto& user : superusers_) {
      std::stringstream drop;
      drop << "DROP USER " << user << ";";
      sql(drop.str());
    }
  }

  enum ColumnIndex { NAME, ID, IS_SUPER, DEFAULT_DB, CAN_LOGIN };

  void assertExpectedFormat(const TQueryResult& result) {
    ASSERT_EQ(result.row_set.is_columnar, true);
    ASSERT_EQ(result.row_set.columns.size(), size_t(5));
    ASSERT_EQ(result.row_set.row_desc[NAME].col_type.type, TDatumType::STR);
    ASSERT_EQ(result.row_set.row_desc[NAME].col_name, "NAME");
    ASSERT_EQ(result.row_set.row_desc[ID].col_type.type, TDatumType::BIGINT);
    ASSERT_EQ(result.row_set.row_desc[ID].col_name, "ID");
    ASSERT_EQ(result.row_set.row_desc[IS_SUPER].col_type.type, TDatumType::BOOL);
    ASSERT_EQ(result.row_set.row_desc[IS_SUPER].col_name, "IS_SUPER");
    ASSERT_EQ(result.row_set.row_desc[DEFAULT_DB].col_type.type, TDatumType::STR);
    ASSERT_EQ(result.row_set.row_desc[DEFAULT_DB].col_name, "DEFAULT_DB");
    ASSERT_EQ(result.row_set.row_desc[CAN_LOGIN].col_type.type, TDatumType::BOOL);
    ASSERT_EQ(result.row_set.row_desc[CAN_LOGIN].col_name, "CAN_LOGIN");
  }

  void assertUserResultFound(const TQueryResult& result, const std::string& username) {
    int num_matches = 0;
    for (size_t i = 0; i < result.row_set.columns[NAME].data.str_col.size(); ++i) {
      if (result.row_set.columns[NAME].data.str_col[i] == username) {
        num_matches++;
      }
    }
    ASSERT_EQ(num_matches, 1);
  }

  template <typename T>
  void assertUserResultFound(const TQueryResult& result,
                             const std::string& username,
                             ColumnIndex col,
                             T val) {
    using CLEANT = std::remove_reference_t<std::remove_cv_t<T>>;
    static_assert(std::is_integral_v<CLEANT> || std::is_floating_point_v<CLEANT> ||
                  std::is_same_v<std::string, CLEANT>);
    int num_matches = 0;
    for (size_t i = 0; i < result.row_set.columns[NAME].data.str_col.size(); ++i) {
      if (result.row_set.columns[NAME].data.str_col[i] == username) {
        num_matches++;
        // integral
        if constexpr (std::is_integral_v<CLEANT>) {  // NOLINT
          ASSERT_EQ(result.row_set.columns[col].data.int_col[i], val);
          // floating point
        } else if constexpr (std::is_floating_point_v<CLEANT>) {  // NOLINT
          ASSERT_EQ(result.row_set.columns[col].data.real_col[i], val);
          // string
        } else if constexpr (std::is_same_v<std::string, CLEANT>) {  // NOLINT
          ASSERT_EQ(result.row_set.columns[col].data.str_col[i], val);
        }
      }
    }
    ASSERT_EQ(num_matches, 1);
  }

  void assertNumUsers(const TQueryResult& result, size_t num_users) {
    ASSERT_EQ(num_users, result.row_set.columns[NAME].data.str_col.size());
  }
  std::vector<std::string> get_users() { return users_; }
  std::vector<std::string> get_superusers() { return superusers_; }

 private:
  static inline std::vector<std::string> users_;
  static inline std::vector<std::string> superusers_;
  static inline std::vector<std::string> dbs_;

  std::string admin_id;
};

TEST_F(ShowUserDetailsTest, AllUsers) {
  TQueryResult result;
  sql(result, "SHOW USER DETAILS;");
  assertExpectedFormat(result);
  assertNumUsers(result, 5);
  assertUserResultFound(result, "admin");
  assertUserResultFound(result, "user1");
  assertUserResultFound(result, "user2");
  assertUserResultFound(result, "super1");
  assertUserResultFound(result, "super2");
}

TEST_F(ShowUserDetailsTest, OneUser) {
  TQueryResult result;
  sql(result, "SHOW USER DETAILS user1;");
  assertNumUsers(result, 1);
  assertUserResultFound(result, "user1");
}

TEST_F(ShowUserDetailsTest, MultipleUsers) {
  TQueryResult result;
  sql(result, "SHOW USER DETAILS user1,super1;");
  assertNumUsers(result, 2);
  assertUserResultFound(result, "user1");
  assertUserResultFound(result, "super1");
}

TEST_F(ShowUserDetailsTest, Columns) {
  using namespace std::string_literals;
  TQueryResult result;
  sql(result, "SHOW USER DETAILS user1;");
  assertNumUsers(result, 1);
  assertUserResultFound(result, "user1", IS_SUPER, false);
  assertUserResultFound(result, "user1", DEFAULT_DB, "omnisci(1)"s);
  assertUserResultFound(result, "user1", CAN_LOGIN, true);
}

class ShowTableDdlTest : public DBHandlerTestFixture {
 protected:
  void SetUp() override {
    DBHandlerTestFixture::SetUp();
    switchToAdmin();
    sql("DROP TABLE IF EXISTS test_table;");
  }

  void TearDown() override {
    switchToAdmin();
    sql("DROP TABLE IF EXISTS test_table;");
    DBHandlerTestFixture::TearDown();
  }

  static void SetUpTestSuite() {
    createDBHandler();
    createTestUser();
  }

  static void TearDownTestSuite() { dropTestUser(); }

  static void createTestUser() {
    sql("CREATE USER test_user (password = 'test_pass');");
    sql("GRANT ACCESS ON DATABASE omnisci TO test_user;");
  }

  static void dropTestUser() { sql("DROP USER IF EXISTS test_user;"); }

  void assertExpectedQueryFormat(const TQueryResult& result) const {
    ASSERT_EQ(result.row_set.is_columnar, true);
    ASSERT_EQ(result.row_set.columns.size(), 1UL);
    ASSERT_EQ(result.row_set.row_desc[0].col_type.type, TDatumType::STR);
    ASSERT_EQ(result.row_set.row_desc[0].col_name, "table_name");
  }

  void assertExpectedQuery(
      const TQueryResult& result,
      const std::vector<std::string>& expected_values,
      const std::vector<std::string>& expected_missing_values) const {
    assertExpectedQueryFormat(result);
    auto& result_values = result.row_set.columns[0].data.str_col;
    // TODO: at the moment, this checks that expected_values are a subset of
    // result_values; once other tests ensure they do not leave behind undropped tables,
    // this can be changed to be a check for equality of expected and result values
    std::unordered_set<std::string> result_values_set(result_values.begin(),
                                                      result_values.end());
    for (auto& value : expected_values) {
      ASSERT_FALSE(result_values_set.find(value) == result_values_set.end());
    }
    for (auto& value : expected_missing_values) {
      ASSERT_TRUE(result_values_set.find(value) == result_values_set.end());
    }
  }

  void assertExpectedQuery(const TQueryResult& result,
                           const std::vector<std::string>& expected_values) const {
    std::vector<std::string> expected_missing_values;
    assertExpectedQuery(result, expected_values, expected_missing_values);
  }

  static void createTestTable() { sql("CREATE TABLE test_table ( test_val int );"); }
};

TEST_F(ShowTableDdlTest, CreateTestTable) {
  createTestTable();
  TQueryResult result;
  std::vector<std::string> expected_result{"test_table"};
  sql(result, "SHOW TABLES;");
  assertExpectedQuery(result, expected_result);
}

TEST_F(ShowTableDdlTest, CreateTwoTestTablesDropOne) {
  createTestTable();
  sql("CREATE TABLE test_table2 ( test_val int );");
  {
    TQueryResult result;
    std::vector<std::string> expected_result{"test_table", "test_table2"};
    sql(result, "SHOW TABLES;");
    assertExpectedQuery(result, expected_result);
  }
  sql("DROP TABLE IF EXISTS test_table;");
  {
    TQueryResult result;
    std::vector<std::string> expected_result{"test_table2"};
    std::vector<std::string> expected_missing_result{"test_table"};
    sql(result, "SHOW TABLES;");
    assertExpectedQuery(result, expected_result, expected_missing_result);
  }
  sql("DROP TABLE test_table2;");
}

TEST_F(ShowTableDdlTest, TestUserSeesNoTables) {
  login("test_user", "test_pass");
  TQueryResult result;
  std::vector<std::string> expected_result{};
  sql(result, "SHOW TABLES;");
  assertExpectedQuery(result, expected_result);
}

TEST_F(ShowTableDdlTest, CreateTestTableDropTestTable) {
  createTestTable();
  sql("DROP TABLE IF EXISTS test_table;");
  TQueryResult result;
  std::vector<std::string> expected_missing_result{"test_table"};
  sql(result, "SHOW TABLES;");
  assertExpectedQuery(result, {}, expected_missing_result);
}

TEST_F(ShowTableDdlTest, TestUserSeesTestTableAfterGrantSelect) {
  createTestTable();
  sql("GRANT SELECT ON TABLE test_table TO test_user;");
  login("test_user", "test_pass");
  TQueryResult result;
  std::vector<std::string> expected_result{"test_table"};
  sql(result, "SHOW TABLES;");
  assertExpectedQuery(result, expected_result);
}

TEST_F(ShowTableDdlTest, TestUserSeesTestTableAfterGrantDrop) {
  createTestTable();
  sql("GRANT DROP ON TABLE test_table TO test_user;");
  login("test_user", "test_pass");
  TQueryResult result;
  std::vector<std::string> expected_result{"test_table"};
  sql(result, "SHOW TABLES;");
  assertExpectedQuery(result, expected_result);
}

TEST_F(ShowTableDdlTest, SuperUserSeesTestTableAfterTestUserCreates) {
  sql("GRANT CREATE TABLE ON DATABASE omnisci TO test_user;");
  login("test_user", "test_pass");
  createTestTable();
  switchToAdmin();
  TQueryResult result;
  std::vector<std::string> expected_result{"test_table"};
  sql(result, "SHOW TABLES;");
  assertExpectedQuery(result, expected_result);
}

TEST_F(ShowTableDdlTest, CreateTableCreateViewAndViewNotSeen) {
  createTestTable();
  sql("CREATE VIEW test_view AS SELECT * from test_table;");
  TQueryResult result;
  std::vector<std::string> expected_result{"test_table"};
  std::vector<std::string> expected_missing_result{"test_view"};
  sql(result, "SHOW TABLES;");
  assertExpectedQuery(result, expected_result, expected_missing_result);
  sql("DROP VIEW test_view;");
}

class ShowRolesTest : public DBHandlerTestFixture {
 protected:
  void SetUp() override { DBHandlerTestFixture::SetUp(); }

  void TearDown() override {
    switchToAdmin();
    DBHandlerTestFixture::TearDown();
  }

  static void SetUpTestSuite() {
    createDBHandler();
    createTestUser("u1", "p1");
    createTestUser("u2", "p2");
  }

  static void TearDownTestSuite() {
    dropTestUser("u1");
    dropTestUser("u2");
  }

  static void createTestUser(const std::string& user_name,
                             const std::string& pass,
                             const bool is_super_user = false) {
    sql("CREATE USER " + user_name + " (password = '" + pass + "', is_super = '" +
        (is_super_user ? "true" : "false") + "');");
    sql("GRANT ALL ON DATABASE omnisci TO " + user_name + ";");
  }

  static void dropTestUser(const std::string& user_name) {
    switchToAdmin();
    sql("DROP USER IF EXISTS " + user_name + ";");
  }

  void assertExpectedResult(const std::vector<std::string> headers,
                            const std::vector<std::vector<std::string>> rows,
                            const TQueryResult& result) {
    const auto& row_set = result.row_set;
    const auto& row_descriptor = result.row_set.row_desc;

    ASSERT_TRUE(row_set.is_columnar);
    ASSERT_EQ(headers.size(), row_descriptor.size());
    ASSERT_FALSE(row_set.columns.empty());

    for (size_t i = 0; i < headers.size(); i++) {
      ASSERT_EQ(row_descriptor[i].col_name, headers[i]);
      ASSERT_EQ(TDatumType::type::STR, row_descriptor[i].col_type.type);
    }

    for (const auto& column : row_set.columns) {
      ASSERT_EQ(rows.size(), column.data.str_col.size());
    }

    for (size_t row = 0; row < rows.size(); row++) {
      for (size_t column = 0; column < rows[row].size(); column++) {
        ASSERT_EQ(rows[row][column], row_set.columns[column].data.str_col[row]);
        ASSERT_FALSE(row_set.columns[column].nulls[row]);
      }
    }
  }
};

TEST_F(ShowRolesTest, SuperUser) {
  sql("CREATE ROLE r1;");
  sql("CREATE ROLE r2;");
  sql("CREATE ROLE r3;");
  sql("CREATE ROLE r4;");
  sql("CREATE ROLE r5;");

  ScopeGuard guard{[&] {
    sql("DROP ROLE r1;");
    sql("DROP ROLE r2;");
    sql("DROP ROLE r3;");
    sql("DROP ROLE r4;");
    sql("DROP ROLE r5;");
  }};

  {
    TQueryResult result;
    EXPECT_NO_THROW(sql(result, "SHOW ROLES;"));
    assertExpectedResult({"ROLES"}, {{"r1"}, {"r2"}, {"r3"}, {"r4"}, {"r5"}}, result);
  }

  {
    TQueryResult result;
    EXPECT_NO_THROW(sql(result, "SHOW EFFECTIVE ROLES;"));
    assertExpectedResult({"ROLES"}, {{"r1"}, {"r2"}, {"r3"}, {"r4"}, {"r5"}}, result);
  }
};

TEST_F(ShowRolesTest, Direct) {
  sql("CREATE ROLE r1;");
  sql("CREATE ROLE r2;");
  sql("CREATE ROLE r3;");
  sql("CREATE ROLE r4;");
  sql("CREATE ROLE r5;");

  ScopeGuard guard{[&] {
    switchToAdmin();
    sql("DROP ROLE r1;");
    sql("DROP ROLE r2;");
    sql("DROP ROLE r3;");
    sql("DROP ROLE r4;");
    sql("DROP ROLE r5;");
  }};

  sql("GRANT r3 TO r1;");
  sql("GRANT r5 TO r3;");

  sql("GRANT r4 TO r2;");
  sql("GRANT r5 TO r4;");

  sql("GRANT r1 TO u1;");
  sql("GRANT r2 TO u1;");

  sql("GRANT r1 TO u2;");
  sql("GRANT r2 TO u2;");
  sql("GRANT r5 TO u2;");

  {
    TQueryResult result;
    login("u1", "p1");
    EXPECT_NO_THROW(sql(result, "SHOW ROLES;"));
    assertExpectedResult({"ROLES"}, {{"r1"}, {"r2"}}, result);
  }

  {
    TQueryResult result;
    login("u2", "p2");
    EXPECT_NO_THROW(sql(result, "SHOW ROLES;"));
    assertExpectedResult({"ROLES"}, {{"r1"}, {"r2"}, {"r5"}}, result);
  }
};

TEST_F(ShowRolesTest, Effective) {
  sql("CREATE ROLE r1;");
  sql("CREATE ROLE r2;");
  sql("CREATE ROLE r3;");
  sql("CREATE ROLE r4;");
  sql("CREATE ROLE r5;");

  ScopeGuard guard{[&] {
    switchToAdmin();
    sql("DROP ROLE r1;");
    sql("DROP ROLE r2;");
    sql("DROP ROLE r3;");
    sql("DROP ROLE r4;");
    sql("DROP ROLE r5;");
  }};

  sql("GRANT r3 TO r1;");
  sql("GRANT r5 TO r3;");

  sql("GRANT r4 TO r2;");
  sql("GRANT r5 TO r4;");

  sql("GRANT r1 TO u1;");
  sql("GRANT r2 TO u2;");

  {
    TQueryResult result;
    login("u1", "p1");
    EXPECT_NO_THROW(sql(result, "SHOW EFFECTIVE ROLES;"));
    assertExpectedResult({"ROLES"}, {{"r1"}, {"r3"}, {"r5"}}, result);
  }

  {
    TQueryResult result;
    login("u2", "p2");
    EXPECT_NO_THROW(sql(result, "SHOW EFFECTIVE ROLES;"));
    assertExpectedResult({"ROLES"}, {{"r2"}, {"r4"}, {"r5"}}, result);
  }
};

TEST_F(ShowRolesTest, Security) {
  sql("CREATE ROLE r1;");
  sql("CREATE ROLE r2;");
  sql("CREATE ROLE r3;");
  sql("CREATE ROLE r4;");
  sql("CREATE ROLE r5;");

  ScopeGuard guard{[&] {
    switchToAdmin();
    sql("DROP ROLE r1;");
    sql("DROP ROLE r2;");
    sql("DROP ROLE r3;");
    sql("DROP ROLE r4;");
    sql("DROP ROLE r5;");
  }};

  sql("GRANT r3 TO r1;");
  sql("GRANT r5 TO r3;");

  sql("GRANT r4 TO r2;");
  sql("GRANT r5 TO r4;");

  sql("GRANT r1 TO u1;");
  sql("GRANT r2 TO u2;");

  {
    TQueryResult result;
    login("u1", "p1");

    EXPECT_NO_THROW(sql(result, "SHOW ROLES u1;"));
    EXPECT_ANY_THROW(sql(result, "SHOW ROLES u2;"));
    EXPECT_NO_THROW(sql(result, "SHOW ROLES r1;"));
    EXPECT_ANY_THROW(sql(result, "SHOW ROLES r2;"));
    EXPECT_NO_THROW(sql(result, "SHOW ROLES r3;"));
    EXPECT_ANY_THROW(sql(result, "SHOW ROLES r4;"));
    EXPECT_NO_THROW(sql(result, "SHOW ROLES r5;"));

    EXPECT_NO_THROW(sql(result, "SHOW EFFECTIVE ROLES u1;"));
    EXPECT_ANY_THROW(sql(result, "SHOW EFFECTIVE ROLES u2;"));
    EXPECT_NO_THROW(sql(result, "SHOW EFFECTIVE ROLES r1;"));
    EXPECT_ANY_THROW(sql(result, "SHOW EFFECTIVE ROLES r2;"));
    EXPECT_NO_THROW(sql(result, "SHOW EFFECTIVE ROLES r3;"));
    EXPECT_ANY_THROW(sql(result, "SHOW EFFECTIVE ROLES r4;"));
    EXPECT_NO_THROW(sql(result, "SHOW EFFECTIVE ROLES r5;"));
  }

  {
    TQueryResult result;
    login("u2", "p2");

    EXPECT_ANY_THROW(sql(result, "SHOW ROLES u1;"));
    EXPECT_NO_THROW(sql(result, "SHOW ROLES u2;"));
    EXPECT_ANY_THROW(sql(result, "SHOW ROLES r1;"));
    EXPECT_NO_THROW(sql(result, "SHOW ROLES r2;"));
    EXPECT_ANY_THROW(sql(result, "SHOW ROLES r3;"));
    EXPECT_NO_THROW(sql(result, "SHOW ROLES r4;"));
    EXPECT_NO_THROW(sql(result, "SHOW ROLES r5;"));

    EXPECT_ANY_THROW(sql(result, "SHOW EFFECTIVE ROLES u1;"));
    EXPECT_NO_THROW(sql(result, "SHOW EFFECTIVE ROLES u2;"));
    EXPECT_ANY_THROW(sql(result, "SHOW EFFECTIVE ROLES r1;"));
    EXPECT_NO_THROW(sql(result, "SHOW EFFECTIVE ROLES r2;"));
    EXPECT_ANY_THROW(sql(result, "SHOW EFFECTIVE ROLES r3;"));
    EXPECT_NO_THROW(sql(result, "SHOW EFFECTIVE ROLES r4;"));
    EXPECT_NO_THROW(sql(result, "SHOW EFFECTIVE ROLES r5;"));
  }
}

class ShowDatabasesTest : public DBHandlerTestFixture {
 protected:
  void SetUp() override { DBHandlerTestFixture::SetUp(); }

  void TearDown() override {
    switchToAdmin();
    sql("DROP DATABASE IF EXISTS test_db_1;");
    sql("DROP DATABASE IF EXISTS test_db_2;");
    DBHandlerTestFixture::TearDown();
  }

  static void SetUpTestSuite() {
    createDBHandler();
    createTestUser("test_user_1", "test_pass_1");
    createTestUser("test_user_2", "test_pass_2");
    createTestUser("test_super_user", "test_pass", true);
  }

  static void TearDownTestSuite() {
    dropTestUser("test_user_1");
    dropTestUser("test_user_2");
    dropTestUser("test_super_user");
  }

  void assertExpectedResult(const std::vector<std::string> headers,
                            const std::vector<std::vector<std::string>> rows,
                            const TQueryResult& result) {
    const auto& row_set = result.row_set;
    const auto& row_descriptor = result.row_set.row_desc;

    ASSERT_TRUE(row_set.is_columnar);
    ASSERT_EQ(headers.size(), row_descriptor.size());
    ASSERT_FALSE(row_set.columns.empty());

    for (size_t i = 0; i < headers.size(); i++) {
      ASSERT_EQ(row_descriptor[i].col_name, headers[i]);
      ASSERT_EQ(TDatumType::type::STR, row_descriptor[i].col_type.type);
    }

    for (const auto& column : row_set.columns) {
      ASSERT_EQ(rows.size(), column.data.str_col.size());
    }

    for (size_t row = 0; row < rows.size(); row++) {
      for (size_t column = 0; column < rows[row].size(); column++) {
        ASSERT_EQ(rows[row][column], row_set.columns[column].data.str_col[row]);
        ASSERT_FALSE(row_set.columns[column].nulls[row]);
      }
    }
  }

  static void createTestUser(const std::string& user_name,
                             const std::string& pass,
                             const bool is_super_user = false) {
    sql("CREATE USER " + user_name + " (password = '" + pass + "', is_super = '" +
        (is_super_user ? "true" : "false") + "');");
  }

  static void dropTestUser(const std::string& user_name) {
    switchToAdmin();
    sql("DROP USER IF EXISTS " + user_name + ";");
  }
};

TEST_F(ShowDatabasesTest, DefaultDatabase) {
  TQueryResult result;
  sql(result, "SHOW DATABASES;");
  // clang-format off
  if (isDistributedMode()) {
    assertExpectedResult({"Database", "Owner"},
                         {{"omnisci", "admin"}},
                         result);
  } else {
    assertExpectedResult({"Database", "Owner"},
                         {{"omnisci", "admin"},
                          {"information_schema", "admin"}},
                     result);
  }
  // clang-format on
}

TEST_F(ShowDatabasesTest, UserCreatedDatabase) {
  sql("CREATE DATABASE test_db_1 (owner = 'test_user_1');");
  login("test_user_1", "test_pass_1", "test_db_1");

  TQueryResult result;
  sql(result, "SHOW DATABASES;");
  // clang-format off
  assertExpectedResult({"Database", "Owner"},
                       {{"test_db_1", "test_user_1"}},
                       result);
  // clang-format on
}

TEST_F(ShowDatabasesTest, OtherUserDatabaseWithNoAccessPrivilege) {
  sql("CREATE DATABASE test_db_1 (owner = 'test_user_1');");
  sql("CREATE DATABASE test_db_2 (owner = 'test_user_2');");
  login("test_user_1", "test_pass_1", "test_db_1");

  TQueryResult result;
  sql(result, "SHOW DATABASES;");
  // clang-format off
  assertExpectedResult({"Database", "Owner"},
                       {{"test_db_1", "test_user_1"}},
                       result);
  // clang-format on
}

TEST_F(ShowDatabasesTest, OtherUserDatabaseWithAccessPrivilege) {
  sql("CREATE DATABASE test_db_1 (owner = 'test_user_1');");
  sql("CREATE DATABASE test_db_2 (owner = 'test_user_2');");
  sql("GRANT ACCESS ON DATABASE test_db_2 to test_user_1;");
  login("test_user_1", "test_pass_1", "test_db_1");

  TQueryResult result;
  sql(result, "SHOW DATABASES;");
  // clang-format off
  assertExpectedResult({"Database", "Owner"},
                       {{"test_db_1", "test_user_1"},
                        {"test_db_2", "test_user_2"}},
                       result);
  // clang-format on
}

TEST_F(ShowDatabasesTest, AdminLoginAndOtherUserDatabases) {
  sql("CREATE DATABASE test_db_1 (owner = 'test_user_1');");
  sql("CREATE DATABASE test_db_2 (owner = 'test_user_2');");

  TQueryResult result;
  sql(result, "SHOW DATABASES;");
  // clang-format off
  if (isDistributedMode()) {
    assertExpectedResult(
        {"Database", "Owner"},
        {{"omnisci", "admin"},
         {"test_db_1", "test_user_1"},
         {"test_db_2", "test_user_2"}},
        result);
  } else {
    assertExpectedResult(
        {"Database", "Owner"},
        {{"omnisci", "admin"},
         {"information_schema", "admin"},
         {"test_db_1", "test_user_1"},
         {"test_db_2", "test_user_2"}},
        result);
  }
  // clang-format on
}

TEST_F(ShowDatabasesTest, SuperUserLoginAndOtherUserDatabases) {
  sql("CREATE DATABASE test_db_1 (owner = 'test_user_1');");
  sql("CREATE DATABASE test_db_2 (owner = 'test_user_2');");
  login("test_super_user", "test_pass");

  TQueryResult result;
  sql(result, "SHOW DATABASES;");
  // clang-format off
  if (isDistributedMode()) {
    assertExpectedResult(
        {"Database", "Owner"},
        {{"omnisci", "admin"},
         {"test_db_1", "test_user_1"},
         {"test_db_2", "test_user_2"}},
        result);
  } else {
    assertExpectedResult(
        {"Database", "Owner"},
        {{"omnisci", "admin"},
         {"information_schema", "admin"},
         {"test_db_1", "test_user_1"},
         {"test_db_2", "test_user_2"}},
        result);
  }
  // clang-format on
}

class ShowCreateTableTest : public DBHandlerTestFixture {
 public:
  void SetUp() override {
    DBHandlerTestFixture::SetUp();
    switchToAdmin();
    sql("DROP TABLE IF EXISTS showcreatetabletest;");
    sql("DROP TABLE IF EXISTS showcreatetabletest1;");
    sql("DROP TABLE IF EXISTS showcreatetabletest2;");
    sql("DROP VIEW IF EXISTS showcreateviewtest;");
    sql("DROP FOREIGN TABLE IF EXISTS test_foreign_table;");
  }

  void TearDown() override {
    switchToAdmin();
    sql("DROP TABLE IF EXISTS showcreatetabletest;");
    sql("DROP TABLE IF EXISTS showcreatetabletest1;");
    sql("DROP TABLE IF EXISTS showcreatetabletest2;");
    sql("DROP VIEW IF EXISTS showcreateviewtest;");
    sql("DROP FOREIGN TABLE IF EXISTS test_foreign_table;");
    DBHandlerTestFixture::TearDown();
  }

  std::string getTestFilePath() {
    return boost::filesystem::canonical("../../Tests/FsiDataFiles/example_1.csv")
        .string();
  }
};

TEST_F(ShowCreateTableTest, Identity) {
  // clang-format off
  std::vector<std::string> creates = {
    "CREATE TABLE showcreatetabletest (\n  i INTEGER)\nWITH (FRAGMENT_SIZE=123);",
    "CREATE TABLE showcreatetabletest (\n  i INTEGER)\nWITH (MAX_CHUNK_SIZE=123);",
    "CREATE TABLE showcreatetabletest (\n  i INTEGER)\nWITH (PAGE_SIZE=123);",
    "CREATE TABLE showcreatetabletest (\n  i INTEGER)\nWITH (MAX_ROWS=123);",
    "CREATE TABLE showcreatetabletest (\n  i INTEGER)\nWITH (VACUUM='IMMEDIATE');",
    "CREATE TABLE showcreatetabletest (\n  i INTEGER)\nWITH (PARTITIONS='SHARDED');",
    "CREATE TABLE showcreatetabletest (\n  i INTEGER)\nWITH (PARTITIONS='REPLICATED');",
    "CREATE TABLE showcreatetabletest (\n  i INTEGER,\n  SHARD KEY (i))\nWITH (SHARD_COUNT=4);",
    "CREATE TABLE showcreatetabletest (\n  i INTEGER)\nWITH (SORT_COLUMN='i');",
    "CREATE TABLE showcreatetabletest (\n  i1 INTEGER,\n  i2 INTEGER)\nWITH (MAX_ROWS=123, VACUUM='IMMEDIATE');",
    "CREATE TABLE showcreatetabletest (\n  id TEXT ENCODING DICT(32),\n  abbr TEXT ENCODING DICT(32),\n  name TEXT ENCODING DICT(32),\n  omnisci_geo GEOMETRY(MULTIPOLYGON, 4326) NOT NULL ENCODING COMPRESSED(32));",
    "CREATE TABLE showcreatetabletest (\n  flight_year SMALLINT,\n  flight_month SMALLINT,\n  flight_dayofmonth SMALLINT,\n  flight_dayofweek SMALLINT,\n  deptime SMALLINT,\n  crsdeptime SMALLINT,\n  arrtime SMALLINT,\n  crsarrtime SMALLINT,\n  uniquecarrier TEXT ENCODING DICT(32),\n  flightnum SMALLINT,\n  tailnum TEXT ENCODING DICT(32),\n  actualelapsedtime SMALLINT,\n  crselapsedtime SMALLINT,\n  airtime SMALLINT,\n  arrdelay SMALLINT,\n  depdelay SMALLINT,\n  origin TEXT ENCODING DICT(32),\n  dest TEXT ENCODING DICT(32),\n  distance SMALLINT,\n  taxiin SMALLINT,\n  taxiout SMALLINT,\n  cancelled SMALLINT,\n  cancellationcode TEXT ENCODING DICT(32),\n  diverted SMALLINT,\n  carrierdelay SMALLINT,\n  weatherdelay SMALLINT,\n  nasdelay SMALLINT,\n  securitydelay SMALLINT,\n  lateaircraftdelay SMALLINT,\n  dep_timestamp TIMESTAMP(0),\n  arr_timestamp TIMESTAMP(0),\n  carrier_name TEXT ENCODING DICT(32),\n  plane_type TEXT ENCODING DICT(32),\n  plane_manufacturer TEXT ENCODING DICT(32),\n  plane_issue_date DATE ENCODING DAYS(32),\n  plane_model TEXT ENCODING DICT(32),\n  plane_status TEXT ENCODING DICT(32),\n  plane_aircraft_type TEXT ENCODING DICT(32),\n  plane_engine_type TEXT ENCODING DICT(32),\n  plane_year SMALLINT,\n  origin_name TEXT ENCODING DICT(32),\n  origin_city TEXT ENCODING DICT(32),\n  origin_state TEXT ENCODING DICT(32),\n  origin_country TEXT ENCODING DICT(32),\n  origin_lat FLOAT,\n  origin_lon FLOAT,\n  dest_name TEXT ENCODING DICT(32),\n  dest_city TEXT ENCODING DICT(32),\n  dest_state TEXT ENCODING DICT(32),\n  dest_country TEXT ENCODING DICT(32),\n  dest_lat FLOAT,\n  dest_lon FLOAT,\n  origin_merc_x FLOAT,\n  origin_merc_y FLOAT,\n  dest_merc_x FLOAT,\n  dest_merc_y FLOAT)\nWITH (FRAGMENT_SIZE=2000000);",
    "CREATE TEMPORARY TABLE showcreatetabletest (\n  i INTEGER);"
  };
  // clang-format on

  for (size_t i = 0; i < creates.size(); ++i) {
    TQueryResult result;
    sql(creates[i]);
    sql(result, "SHOW CREATE TABLE showcreatetabletest;");
    EXPECT_EQ(creates[i], result.row_set.columns[0].data.str_col[0]);
    sql("DROP TABLE IF EXISTS showcreatetabletest;");
  }
}

TEST_F(ShowCreateTableTest, Defaults) {
  std::vector<std::string> creates = {
      "CREATE TABLE showcreatetabletest (i INTEGER) WITH (FRAGMENT_SIZE=" +
          std::to_string(DEFAULT_FRAGMENT_ROWS) + ");",
      "CREATE TABLE showcreatetabletest (i INTEGER) WITH (MAX_CHUNK_SIZE=" +
          std::to_string(DEFAULT_MAX_CHUNK_SIZE) + ");",
      "CREATE TABLE showcreatetabletest (i INTEGER) WITH (PAGE_SIZE=" +
          std::to_string(DEFAULT_PAGE_SIZE) + ");",
      "CREATE TABLE showcreatetabletest (i INTEGER) WITH (MAX_ROWS=" +
          std::to_string(DEFAULT_MAX_ROWS) + ");",
      "CREATE TABLE showcreatetabletest (i INTEGER) WITH (VACUUM='DELAYED');"};

  for (size_t i = 0; i < creates.size(); ++i) {
    sql(creates[i]);
    TQueryResult result;
    sql(result, "SHOW CREATE TABLE showcreatetabletest;");
    EXPECT_EQ("CREATE TABLE showcreatetabletest (\n  i INTEGER);",
              result.row_set.columns[0].data.str_col[0]);
    sql("DROP TABLE IF EXISTS showcreatetabletest;");
  }
}

TEST_F(ShowCreateTableTest, Other) {
  {
    sql("CREATE TABLE showcreatetabletest (i INTEGER);");
    std::string sqltext =
        "CREATE VIEW showcreateviewtest AS SELECT * FROM showcreatetabletest;";
    sql(sqltext);
    TQueryResult result;
    sql(result, "SHOW CREATE TABLE showcreateviewtest;");
    EXPECT_EQ(sqltext, result.row_set.columns[0].data.str_col[0]);
    sql("DROP VIEW IF EXISTS showcreateviewtest;");
    sql("DROP TABLE IF EXISTS showcreatetabletest;");
  }

  {
    sql("CREATE TABLE showcreatetabletest1 (\n  t TEXT ENCODING DICT(32));");
    std::string sqltext =
        "CREATE TABLE showcreatetabletest2 (\n  t TEXT,\n  SHARED DICTIONARY (t) "
        "REFERENCES showcreatetabletest1(t))\nWITH (SORT_COLUMN='t');";
    sql(sqltext);
    TQueryResult result;
    sql(result, "SHOW CREATE TABLE showcreatetabletest2;");
    EXPECT_EQ(sqltext, result.row_set.columns[0].data.str_col[0]);
    sql("DROP TABLE IF EXISTS showcreatetabletest1;");
    sql("DROP TABLE IF EXISTS showcreatetabletest2;");
  }
}

TEST_F(ShowCreateTableTest, SharedComplex) {
  {
    sql("DROP TABLE IF EXISTS showcreatetabletest1;");
    sql("DROP TABLE IF EXISTS renamedcreatetabletest1;");
    sql("DROP TABLE IF EXISTS showcreatetabletest2;");
    sql("DROP TABLE IF EXISTS showcreatetabletest3;");

    sql("CREATE TABLE showcreatetabletest1 (\n  t1 TEXT ENCODING DICT(16));");
    std::string sqltext =
        "CREATE TABLE showcreatetabletest2 (\n  t2 TEXT,\n  SHARED DICTIONARY (t2) "
        "REFERENCES showcreatetabletest1(t1));";
    sql(sqltext);
    {
      TQueryResult result;
      sql(result, "SHOW CREATE TABLE showcreatetabletest2;");
      EXPECT_EQ(sqltext, result.row_set.columns[0].data.str_col[0]);
    }
    sql("CREATE TABLE showcreatetabletest3 (\n  t3 TEXT,\n SHARED DICTIONARY (t3) "
        "REFERENCES showcreatetabletest2(t2));");

    sql("ALTER TABLE showcreatetabletest1 RENAME TO renamedcreatetabletest1;");

    {
      TQueryResult result;
      sql(result, "SHOW CREATE TABLE showcreatetabletest3;");
      EXPECT_EQ(
          "CREATE TABLE showcreatetabletest3 (\n  t3 TEXT,\n  SHARED DICTIONARY (t3) "
          "REFERENCES renamedcreatetabletest1(t1));",
          result.row_set.columns[0].data.str_col[0]);
    }
    sql("DROP TABLE IF EXISTS renamedcreatetabletest1;");

    {
      TQueryResult result;
      sql(result, "SHOW CREATE TABLE showcreatetabletest2;");
      EXPECT_EQ("CREATE TABLE showcreatetabletest2 (\n  t2 TEXT ENCODING DICT(16));",
                result.row_set.columns[0].data.str_col[0]);
    }
    {
      TQueryResult result;
      sql(result, "SHOW CREATE TABLE showcreatetabletest3;");
      EXPECT_EQ(
          "CREATE TABLE showcreatetabletest3 (\n  t3 TEXT,\n  SHARED DICTIONARY (t3) "
          "REFERENCES showcreatetabletest2(t2));",
          result.row_set.columns[0].data.str_col[0]);
    }
    sql("DROP TABLE IF EXISTS showcreatetabletest2;");
    {
      TQueryResult result;
      sql(result, "SHOW CREATE TABLE showcreatetabletest3;");
      EXPECT_EQ("CREATE TABLE showcreatetabletest3 (\n  t3 TEXT ENCODING DICT(16));",
                result.row_set.columns[0].data.str_col[0]);
    }
    sql("DROP TABLE IF EXISTS showcreatetabletest3;");
  }
}

TEST_F(ShowCreateTableTest, TextArray) {
  sql("CREATE TABLE showcreatetabletest (t1 TEXT[], t2 TEXT[5]);");
  sqlAndCompareResult("SHOW CREATE TABLE showcreatetabletest;",
                      {{"CREATE TABLE showcreatetabletest (\n  t1 TEXT[] ENCODING "
                        "DICT(32),\n  t2 TEXT[5] ENCODING DICT(32));"}});
}

TEST_F(ShowCreateTableTest, TimestampArray) {
  sql("CREATE TABLE showcreatetabletest (tp TIMESTAMP, tpe TIMESTAMP ENCODING "
      "FIXED(32), "
      "tp1 TIMESTAMP(3), tp2 "
      "TIMESTAMP(6)[], tp3 TIMESTAMP(9)[2]);");
  sqlAndCompareResult("SHOW CREATE TABLE showcreatetabletest;",
                      {{"CREATE TABLE showcreatetabletest (\n  tp TIMESTAMP(0),\n  tpe "
                        "TIMESTAMP(0) ENCODING FIXED(32),\n  tp1 TIMESTAMP(3),\n  "
                        "tp2 TIMESTAMP(6)[],\n  tp3 TIMESTAMP(9)[2]);"}});
}

TEST_F(ShowCreateTableTest, TimestampEncoding) {
  // Timestamp encoding accepts a shorthand syntax (see above). Ensure the output of the
  // SHOW CREATE TABLE command using the short hand syntax can be passed back in as
  // input.
  sql("CREATE TABLE showcreatetabletest (tp TIMESTAMP(0), tpe TIMESTAMP(0) ENCODING "
      "FIXED(32));");
  sqlAndCompareResult("SHOW CREATE TABLE showcreatetabletest;",
                      {{"CREATE TABLE showcreatetabletest (\n  tp TIMESTAMP(0),\n  tpe "
                        "TIMESTAMP(0) ENCODING FIXED(32));"}});
}

TEST_F(ShowCreateTableTest, ForeignTable_Defaults) {
  sql("CREATE FOREIGN TABLE test_foreign_table(b BOOLEAN, bint BIGINT, i INTEGER, sint "
      "SMALLINT, tint TINYINT, f FLOAT, d DOUBLE, dc DECIMAL(5, 2), t TEXT, tm TIME, "
      "tstamp "
      "TIMESTAMP, dt DATE, i_array INTEGER[], t_array TEXT[5], p POINT, l LINESTRING, "
      "poly POLYGON, mpoly MULTIPOLYGON) "
      "SERVER omnisci_local_csv "
      "WITH (file_path = '" +
      getTestFilePath() + "');");
  sqlAndCompareResult(
      "SHOW CREATE TABLE test_foreign_table;",
      {{"CREATE FOREIGN TABLE test_foreign_table (\n  b BOOLEAN,\n  bint BIGINT,\n  i "
        "INTEGER,\n  sint SMALLINT,\n  tint TINYINT,\n  f FLOAT,\n  d DOUBLE,\n  dc "
        "DECIMAL(5,2) ENCODING FIXED(32),\n  t TEXT ENCODING DICT(32),\n  tm TIME,\n  "
        "tstamp TIMESTAMP(0),\n  dt DATE ENCODING DAYS(32),\n  i_array INTEGER[],\n  "
        "t_array TEXT[5] ENCODING DICT(32),\n  p GEOMETRY(POINT) ENCODING NONE,\n  l "
        "GEOMETRY(LINESTRING) ENCODING NONE,\n  poly GEOMETRY(POLYGON) ENCODING "
        "NONE,\n  mpoly GEOMETRY(MULTIPOLYGON) ENCODING NONE)"
        "\nSERVER omnisci_local_csv"
        "\nWITH (FILE_PATH='" +
        getTestFilePath() +
        "', REFRESH_TIMING_TYPE='MANUAL', REFRESH_UPDATE_TYPE='ALL');"}});
}

TEST_F(ShowCreateTableTest, ForeignTable_WithEncodings) {
  sql("CREATE FOREIGN TABLE test_foreign_table(bint BIGINT ENCODING FIXED(16), i "
      "INTEGER "
      "ENCODING FIXED(8), sint SMALLINT ENCODING FIXED(8), t1 TEXT ENCODING DICT(16), "
      "t2 "
      "TEXT ENCODING NONE, tm TIME ENCODING FIXED(32), tstamp TIMESTAMP(3), tstamp2 "
      "TIMESTAMP ENCODING FIXED(32), dt DATE ENCODING DAYS(16), p GEOMETRY(POINT, "
      "4326), "
      "l GEOMETRY(LINESTRING, 4326) ENCODING COMPRESSED(32), "
      "poly GEOMETRY(POLYGON, 4326) ENCODING NONE, "
      "mpoly GEOMETRY(MULTIPOLYGON, 900913)) "
      "SERVER omnisci_local_csv "
      "WITH (file_path = '" +
      getTestFilePath() + "');");
  sqlAndCompareResult(
      "SHOW CREATE TABLE test_foreign_table;",
      {{"CREATE FOREIGN TABLE test_foreign_table (\n  bint BIGINT ENCODING "
        "FIXED(16),\n  "
        "i INTEGER ENCODING FIXED(8),\n  sint SMALLINT ENCODING FIXED(8),\n  t1 TEXT "
        "ENCODING DICT(16),\n  t2 TEXT ENCODING NONE,\n  tm TIME ENCODING FIXED(32),\n "
        " "
        "tstamp TIMESTAMP(3),\n  tstamp2 TIMESTAMP(0) ENCODING FIXED(32),\n  dt DATE "
        "ENCODING DAYS(16),\n  p GEOMETRY(POINT, 4326) ENCODING COMPRESSED(32),\n  l "
        "GEOMETRY(LINESTRING, 4326) ENCODING COMPRESSED(32),\n  poly GEOMETRY(POLYGON, "
        "4326) ENCODING NONE,\n  mpoly GEOMETRY(MULTIPOLYGON, 900913) ENCODING NONE)"
        "\nSERVER omnisci_local_csv"
        "\nWITH (FILE_PATH='" +
        getTestFilePath() +
        "', REFRESH_TIMING_TYPE='MANUAL', REFRESH_UPDATE_TYPE='ALL');"}});
}

TEST_F(ShowCreateTableTest, ForeignTable_AllOptions) {
  std::time_t timestamp = std::time(0) + (60 * 60);
  std::tm* gmt_time = std::gmtime(&timestamp);
  constexpr int buffer_size = 256;
  char buffer[buffer_size];
  std::strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", gmt_time);
  std::string start_date_time = buffer;

  sql("CREATE FOREIGN TABLE test_foreign_table(i INTEGER) "
      "SERVER omnisci_local_csv "
      "WITH (file_path = '" +
      getTestFilePath() +
      "', fragment_size = 50, refresh_update_type = 'append', refresh_timing_type = "
      "'scheduled', refresh_start_date_time = '" +
      start_date_time +
      "', refresh_interval= '5H', array_delimiter = '_', array_marker = '[]', "
      "buffer_size = '100', delimiter = '|', escape = '\\', header = 'false', "
      "line_delimiter = '.', lonlat = 'false', nulls = 'NIL', "
      "quote = '`', quoted = 'false');");
  sqlAndCompareResult("SHOW CREATE TABLE test_foreign_table;",
                      {{"CREATE FOREIGN TABLE test_foreign_table (\n  i INTEGER)"
                        "\nSERVER omnisci_local_csv"
                        "\nWITH (ARRAY_DELIMITER='_', ARRAY_MARKER='[]', "
                        "BUFFER_SIZE='100', DELIMITER='|', ESCAPE='\\', "
                        "FILE_PATH='" +
                        getTestFilePath() +
                        "', FRAGMENT_SIZE='50', HEADER='false', LINE_DELIMITER='.', "
                        "LONLAT='false', NULLS='NIL', QUOTE='`', QUOTED='false', "
                        "REFRESH_INTERVAL='5H', "
                        "REFRESH_START_DATE_TIME='" +
                        start_date_time +
                        "', REFRESH_TIMING_TYPE='SCHEDULED', "
                        "REFRESH_UPDATE_TYPE='APPEND', FRAGMENT_SIZE=50);"}});
}

TEST_F(ShowCreateTableTest, NotCaseSensitive) {
  sql("CREATE TABLE showcreatetabletest(c1 int);");

  sqlAndCompareResult("SHOW CREATE TABLE sHoWcReAtEtAbLeTeSt;",
                      {{"CREATE TABLE showcreatetabletest (\n  c1 INTEGER);"}});
}

TEST_F(ShowCreateTableTest, TableWithUncappedEpoch) {
  sql("CREATE TABLE showcreatetabletest (c1 INTEGER);");
  getCatalog().setUncappedTableEpoch("showcreatetabletest");
  sqlAndCompareResult("SHOW CREATE TABLE showcreatetabletest;",
                      {{"CREATE TABLE showcreatetabletest (\n  c1 INTEGER);"}});
}

TEST_F(ShowCreateTableTest, TableWithMaxRollbackEpochs) {
  sql("CREATE TABLE showcreatetabletest (c1 INTEGER) WITH (MAX_ROLLBACK_EPOCHS = 10);");
  sqlAndCompareResult("SHOW CREATE TABLE showcreatetabletest;",
                      {{"CREATE TABLE showcreatetabletest (\n  c1 INTEGER)\nWITH "
                        "(MAX_ROLLBACK_EPOCHS=10);"}});
}

TEST_F(ShowCreateTableTest, DefaultColumnValues) {
  sql("CREATE TABLE showcreatetabletest (idx INTEGER NOT NULL, i INTEGER DEFAULT 14,"
      "big_i BIGINT DEFAULT 314958734, null_i INTEGER, int_a INTEGER[] "
      "DEFAULT ARRAY[1, 2, 3], text_a TEXT[] DEFAULT ARRAY['a', 'b'] ENCODING DICT(32),"
      "dt TEXT DEFAULT 'World' ENCODING DICT(32), ls GEOMETRY(LINESTRING) "
      "DEFAULT 'LINESTRING (1 1,2 2,3 3)' ENCODING NONE, p GEOMETRY(POINT) DEFAULT "
      "'POINT (1 2)' ENCODING NONE,  d DATE DEFAULT '2011-10-23' ENCODING DAYS(32), "
      "ta TIMESTAMP[] DEFAULT ARRAY['2011-10-23 07:15:01', '2012-09-17 11:59:11'], "
      "f FLOAT DEFAULT 1.15, n DECIMAL(3,2) DEFAULT 1.25 ENCODING FIXED(16));");
  sqlAndCompareResult(
      "SHOW CREATE TABLE showcreatetabletest;",
      {{"CREATE TABLE showcreatetabletest (\n  idx INTEGER NOT NULL,\n  i INTEGER "
        "DEFAULT 14,\n  big_i "
        "BIGINT DEFAULT 314958734,\n  null_i INTEGER,\n  int_a INTEGER[] DEFAULT "
        "ARRAY[1, 2, 3],\n  text_a TEXT[] DEFAULT ARRAY['a', 'b'] ENCODING DICT(32),\n  "
        "dt TEXT DEFAULT 'World' ENCODING DICT(32),\n  ls GEOMETRY(LINESTRING) DEFAULT "
        "'LINESTRING (1 1,2 2,3 3)' ENCODING NONE,\n  p GEOMETRY(POINT) DEFAULT 'POINT "
        "(1 2)' ENCODING NONE,\n  d DATE DEFAULT '2011-10-23' ENCODING DAYS(32),\n  ta "
        "TIMESTAMP(0)[] DEFAULT ARRAY['2011-10-23 07:15:01', '2012-09-17 11:59:11'],\n  "
        "f "
        "FLOAT DEFAULT 1.15,\n  n DECIMAL(3,2) DEFAULT 1.25 ENCODING FIXED(16));"}});
}

class SystemTablesShowCreateTableTest : public ShowCreateTableTest {
 protected:
  void SetUp() override {
    if (isDistributedMode()) {
      GTEST_SKIP() << "Test is not supported in distributed mode.";
    }
    login("admin", "HyperInteractive", "information_schema");
  }
};

TEST_F(SystemTablesShowCreateTableTest, Users) {
  sqlAndCompareResult(
      "SHOW CREATE TABLE users;",
      {{"CREATE TABLE users (\n  user_id INTEGER,\n  user_name TEXT ENCODING DICT(32),\n "
        " "
        "is_super_user BOOLEAN,\n  default_db_id INTEGER,\n  can_login BOOLEAN);"}});
}

TEST_F(SystemTablesShowCreateTableTest, Tables) {
  sqlAndCompareResult(
      "SHOW CREATE TABLE tables;",
      {{"CREATE TABLE tables (\n  database_id INTEGER,\n  table_id INTEGER,\n  "
        "table_name TEXT ENCODING DICT(32),\n  owner_id INTEGER,\n  column_count "
        "INTEGER,\n  is_view BOOLEAN,\n  view_sql TEXT ENCODING DICT(32),\n  "
        "max_fragment_size INTEGER,\n  max_chunk_size BIGINT,\n  fragment_page_size "
        "INTEGER,\n  max_rows BIGINT,\n  max_rollback_epochs INTEGER,\n  shard_count "
        "INTEGER);"}});
}

TEST_F(SystemTablesShowCreateTableTest, Dashboards) {
  sqlAndCompareResult(
      "SHOW CREATE TABLE dashboards;",
      {{"CREATE TABLE dashboards (\n  database_id INTEGER,\n  dashboard_id INTEGER,\n  "
        "dashboard_name TEXT ENCODING DICT(32),\n  owner_id INTEGER,\n  last_updated_at "
        "TIMESTAMP(0));"}});
}

TEST_F(SystemTablesShowCreateTableTest, Databases) {
  sqlAndCompareResult("SHOW CREATE TABLE databases;",
                      {{"CREATE TABLE databases (\n  database_id INTEGER,\n  "
                        "database_name TEXT ENCODING DICT(32),\n  owner_id INTEGER);"}});
}

TEST_F(SystemTablesShowCreateTableTest, Permissions) {
  sqlAndCompareResult(
      "SHOW CREATE TABLE permissions;",
      {{"CREATE TABLE permissions (\n  role_name TEXT ENCODING DICT(32),\n  is_user_role "
        "BOOLEAN,\n  database_id INTEGER,\n  object_name TEXT ENCODING DICT(32),\n  "
        "object_id INTEGER,\n  object_owner_id INTEGER,\n  object_permission_type TEXT "
        "ENCODING DICT(32),\n  object_permissions TEXT[] ENCODING DICT(32));"}});
}

TEST_F(SystemTablesShowCreateTableTest, RoleAssignments) {
  sqlAndCompareResult("SHOW CREATE TABLE role_assignments;",
                      {{"CREATE TABLE role_assignments (\n  role_name TEXT ENCODING "
                        "DICT(32),\n  user_name TEXT ENCODING DICT(32));"}});
}

TEST_F(SystemTablesShowCreateTableTest, Roles) {
  sqlAndCompareResult("SHOW CREATE TABLE roles;",
                      {{"CREATE TABLE roles (\n  role_name TEXT ENCODING DICT(32));"}});
}

TEST_F(SystemTablesShowCreateTableTest, MemorySummary) {
  sqlAndCompareResult(
      "SHOW CREATE TABLE memory_summary;",
      {{"CREATE TABLE memory_summary (\n  node TEXT ENCODING DICT(32),\n  device_id "
        "INTEGER,\n  device_type TEXT ENCODING DICT(32),\n  max_page_count BIGINT,\n  "
        "page_size BIGINT,\n  allocated_page_count BIGINT,\n  used_page_count BIGINT,\n  "
        "free_page_count BIGINT);"}});
}

TEST_F(SystemTablesShowCreateTableTest, MemoryDetails) {
  sqlAndCompareResult(
      "SHOW CREATE TABLE memory_details;",
      {{"CREATE TABLE memory_details (\n  node TEXT ENCODING DICT(32),\n  database_id "
        "INTEGER,\n  table_id INTEGER,\n  column_id INTEGER,\n  chunk_key INTEGER[],\n  "
        "device_id INTEGER,\n  device_type TEXT ENCODING DICT(32),\n  memory_status TEXT "
        "ENCODING DICT(32),\n  page_count BIGINT,\n  page_size BIGINT,\n  slab_id "
        "INTEGER,\n  start_page BIGINT,\n  last_touch_epoch BIGINT);"}});
}

namespace {
const int64_t PAGES_PER_DATA_FILE =
    File_Namespace::FileMgr::DEFAULT_NUM_PAGES_PER_DATA_FILE;
const int64_t PAGES_PER_METADATA_FILE =
    File_Namespace::FileMgr::DEFAULT_NUM_PAGES_PER_METADATA_FILE;
const int64_t DEFAULT_DATA_FILE_SIZE{DEFAULT_PAGE_SIZE * PAGES_PER_DATA_FILE};
const int64_t DEFAULT_METADATA_FILE_SIZE{METADATA_PAGE_SIZE * PAGES_PER_METADATA_FILE};
}  // namespace

class ShowDiskCacheUsageTest : public DBHandlerTestFixture {
 public:
  static inline constexpr int64_t epoch_file_size{2 * sizeof(int64_t)};
  static inline constexpr int64_t empty_mgr_size{0};
  static inline constexpr int64_t chunk_size{DEFAULT_PAGE_SIZE + METADATA_PAGE_SIZE};
  // TODO(Misiu): These can be made constexpr once c++20 is supported.
  static inline std::string cache_path_ = to_string(BASE_PATH) + "/omnisci_disk_cache";
  static inline std::string foreign_table1{"foreign_table1"};
  static inline std::string foreign_table2{"foreign_table2"};
  static inline std::string foreign_table3{"foreign_table3"};
  static inline std::string table1{"table1"};

  static void SetUpTestSuite() {
    DBHandlerTestFixture::SetUpTestSuite();
    loginAdmin();
    sql("DROP DATABASE IF EXISTS test_db;");
    sql("CREATE DATABASE test_db;");
    login("admin", "HyperInteractive", "test_db");
    getCatalog().getDataMgr().getPersistentStorageMgr()->getDiskCache()->clear();
  }

  static void TearDownTestSuite() {
    sql("DROP DATABASE IF EXISTS test_db;");
    sql("DROP USER IF EXISTS test_user;");
    DBHandlerTestFixture::TearDownTestSuite();
  }

  void SetUp() override {
    if (isDistributedMode()) {
      GTEST_SKIP() << "Test not supported in distributed mode.";
    }
    DBHandlerTestFixture::SetUp();
    login("admin", "HyperInteractive", "test_db");
    sql("DROP FOREIGN TABLE IF EXISTS " + foreign_table1 + ";");
    sql("DROP FOREIGN TABLE IF EXISTS " + foreign_table2 + ";");
    sql("DROP FOREIGN TABLE IF EXISTS " + foreign_table3 + ";");
    sql("DROP TABLE IF EXISTS " + table1 + ";");
  }

  void TearDown() override {
    sql("DROP FOREIGN TABLE IF EXISTS " + foreign_table1 + ";");
    sql("DROP FOREIGN TABLE IF EXISTS " + foreign_table2 + ";");
    sql("DROP FOREIGN TABLE IF EXISTS " + foreign_table3 + ";");
    sql("DROP TABLE IF EXISTS " + table1 + ";");
    DBHandlerTestFixture::TearDown();
  }

  void sqlCreateBasicForeignTable(std::string& table_name) {
    sql("CREATE FOREIGN TABLE " + table_name +
        " (i INTEGER) SERVER omnisci_local_parquet WITH "
        "(file_path = '" +
        boost::filesystem::canonical("../../Tests/FsiDataFiles/0.parquet").string() +
        "');");
  }

  uint64_t getWrapperSizeForTable(const std::string& table_name) {
    uint64_t space_used = 0;
    auto& cat = getCatalog();
    auto td = cat.getMetadataForTable(table_name, false);
    std::string table_dir =
        cache_path_ + "/" +
        File_Namespace::get_dir_name_for_table(cat.getDatabaseId(), td->tableId);
    if (boost::filesystem::exists(table_dir)) {
      for (const auto& file :
           boost::filesystem::recursive_directory_iterator(table_dir)) {
        if (boost::filesystem::is_regular_file(file.path())) {
          space_used += boost::filesystem::file_size(file.path());
        }
      }
    }
    return space_used;
  }

  uint64_t getMinSizeForTable(std::string& table_name) {
    return chunk_size + getWrapperSizeForTable(table_name);
  }
};

TEST_F(ShowDiskCacheUsageTest, SingleTable) {
  sqlCreateBasicForeignTable(foreign_table1);

  sqlAndCompareResult("SHOW DISK CACHE USAGE;", {{foreign_table1, empty_mgr_size}});
}

TEST_F(ShowDiskCacheUsageTest, SingleTableInUse) {
  sqlCreateBasicForeignTable(foreign_table1);

  sql("SELECT * FROM " + foreign_table1 + ";");
  sqlAndCompareResult("SHOW DISK CACHE USAGE;",
                      {{foreign_table1, i(getMinSizeForTable(foreign_table1))}});
}

TEST_F(ShowDiskCacheUsageTest, MultipleTables) {
  sqlCreateBasicForeignTable(foreign_table1);
  sqlCreateBasicForeignTable(foreign_table2);
  sqlCreateBasicForeignTable(foreign_table3);

  sql("SELECT * FROM " + foreign_table1 + ";");
  sql("SELECT * FROM " + foreign_table2 + ";");

  sqlAndCompareResult("SHOW DISK CACHE USAGE;",
                      {{foreign_table1, i(getMinSizeForTable(foreign_table1))},
                       {foreign_table2, i(getMinSizeForTable(foreign_table2))},
                       {foreign_table3, empty_mgr_size}});
}

TEST_F(ShowDiskCacheUsageTest, NoTables) {
  sqlAndCompareResult("SHOW DISK CACHE USAGE;", {});
}

TEST_F(ShowDiskCacheUsageTest, NoTablesFiltered) {
  queryAndAssertException("SHOW DISK CACHE USAGE foreign_table;",
                          "Can not show disk cache usage for table: "
                          "foreign_table. Table does not exist.");
}

TEST_F(ShowDiskCacheUsageTest, MultipleTablesFiltered) {
  sqlCreateBasicForeignTable(foreign_table1);
  sqlCreateBasicForeignTable(foreign_table2);
  sqlCreateBasicForeignTable(foreign_table3);

  sql("SELECT * FROM " + foreign_table1 + ";");
  sql("SELECT * FROM " + foreign_table2 + ";");

  sqlAndCompareResult(
      "SHOW DISK CACHE USAGE " + foreign_table1 + ", " + foreign_table3 + ";",
      {{foreign_table1, i(getMinSizeForTable(foreign_table1))},
       {foreign_table3, empty_mgr_size}});
}

TEST_F(ShowDiskCacheUsageTest, SingleTableDropped) {
  sqlCreateBasicForeignTable(foreign_table1);

  sql("SELECT * FROM " + foreign_table1 + ";");
  sql("DROP FOREIGN TABLE " + foreign_table1 + ";");

  sqlAndCompareResult("SHOW DISK CACHE USAGE;", {});
}

TEST_F(ShowDiskCacheUsageTest, SingleTableEvicted) {
  sqlCreateBasicForeignTable(foreign_table1);

  sql("SELECT * FROM " + foreign_table1 + ";");
  sql("REFRESH FOREIGN TABLES " + foreign_table1 + " WITH (evict=true);");

  sqlAndCompareResult("SHOW DISK CACHE USAGE;", {{foreign_table1, empty_mgr_size}});
}

TEST_F(ShowDiskCacheUsageTest, SingleTableRefreshed) {
  sqlCreateBasicForeignTable(foreign_table1);

  sql("SELECT * FROM " + foreign_table1 + ";");
  sql("REFRESH FOREIGN TABLES " + foreign_table1 + ";");

  sqlAndCompareResult("SHOW DISK CACHE USAGE;",
                      {{foreign_table1, i(getMinSizeForTable(foreign_table1))}});
}

TEST_F(ShowDiskCacheUsageTest, SingleTableMetadataOnly) {
  sqlCreateBasicForeignTable(foreign_table1);

  sql("SELECT COUNT(*) FROM " + foreign_table1 + ";");

  sqlAndCompareResult(
      "SHOW DISK CACHE USAGE;",
      {{foreign_table1, i(METADATA_PAGE_SIZE + getWrapperSizeForTable(foreign_table1))}});
}

TEST_F(ShowDiskCacheUsageTest, ForeignAndNormalTable) {
  sqlCreateBasicForeignTable(foreign_table1);
  sql("CREATE TABLE " + table1 + " (s TEXT);");

  sql("SELECT * FROM " + foreign_table1 + ";");
  sql("SELECT * FROM " + table1 + ";");

  sqlAndCompareResult(
      "SHOW DISK CACHE USAGE;",
      {{foreign_table1, i(getMinSizeForTable(foreign_table1))}, {table1, i(0)}});
}

TEST_F(ShowDiskCacheUsageTest, MultipleChunks) {
  sql("CREATE FOREIGN TABLE " + foreign_table1 +
      " (t TEXT, i INTEGER[]) SERVER omnisci_local_parquet WITH "
      "(file_path = '" +
      boost::filesystem::canonical("../../Tests/FsiDataFiles/example_1.parquet")
          .string() +
      "');");
  sql("SELECT * FROM " + foreign_table1 + ";");
  sqlAndCompareResult("SHOW DISK CACHE USAGE;",
                      {{foreign_table1,
                        i(getMinSizeForTable(foreign_table1) +
                          (2 * (METADATA_PAGE_SIZE + DEFAULT_PAGE_SIZE)))}});
}

class ShowDiskCacheUsageForNormalTableTest : public ShowDiskCacheUsageTest {
 public:
  static void SetUpTestSuite() {
    ShowDiskCacheUsageTest::SetUpTestSuite();
    resetPersistentStorageMgr(File_Namespace::DiskCacheLevel::all);
  }

  static void TearDownTestSuite() {
    resetPersistentStorageMgr(File_Namespace::DiskCacheLevel::fsi);
    ShowDiskCacheUsageTest::TearDownTestSuite();
  }

  static void resetPersistentStorageMgr(File_Namespace::DiskCacheLevel cache_level) {
    for (auto table_it : getCatalog().getAllTableMetadata()) {
      getCatalog().removeFragmenterForTable(table_it->tableId);
    }
    getCatalog().getDataMgr().resetPersistentStorage(
        {cache_path_, cache_level}, 0, getSystemParameters());
  }
};

TEST_F(ShowDiskCacheUsageForNormalTableTest, NormalTableEmptyUninitialized) {
  sqlCreateBasicForeignTable(foreign_table1);
  sql("CREATE TABLE " + table1 + " (s TEXT);");

  sql("SELECT * FROM " + foreign_table1 + ";");

  sqlAndCompareResult("SHOW DISK CACHE USAGE;",
                      {{foreign_table1, i(getMinSizeForTable(foreign_table1))},
                       {table1, empty_mgr_size}});
}

// If a table is initialized, but empty (it has a fileMgr, but no content), it will have
// created an epoch file, so it returns the size of that file only.  This is different
// from the case where no manager is found which returns 0.
TEST_F(ShowDiskCacheUsageForNormalTableTest, NormalTableEmptyInitialized) {
  sqlCreateBasicForeignTable(foreign_table1);
  sql("CREATE TABLE " + table1 + " (s TEXT);");

  sql("SELECT * FROM " + foreign_table1 + ";");
  sql("SELECT * FROM " + table1 + ";");

  sqlAndCompareResult("SHOW DISK CACHE USAGE;",
                      {{foreign_table1, i(getMinSizeForTable(foreign_table1))},
                       {table1, empty_mgr_size}});
}

TEST_F(ShowDiskCacheUsageForNormalTableTest, NormalTableMinimum) {
  sqlCreateBasicForeignTable(foreign_table1);
  sql("CREATE TABLE " + table1 + " (s TEXT);");

  sql("SELECT * FROM " + foreign_table1 + ";");
  sql("INSERT INTO " + table1 + " VALUES('1');");

  sqlAndCompareResult("SHOW DISK CACHE USAGE;",
                      {{foreign_table1, i(getMinSizeForTable(foreign_table1))},
                       {table1, i(chunk_size * 2 + getWrapperSizeForTable(table1))}});
}

class ShowTableDetailsTest : public DBHandlerTestFixture,
                             public testing::WithParamInterface<int32_t> {
 protected:
  void SetUp() override {
    DBHandlerTestFixture::SetUp();
    login("admin", "HyperInteractive", "test_db");
    dropTestTables();
  }

  void TearDown() override {
    login("admin", "HyperInteractive", "test_db");
    dropTestTables();
    DBHandlerTestFixture::TearDown();
  }

  static void SetUpTestSuite() {
    DBHandlerTestFixture::SetUpTestSuite();
    switchToAdmin();
    sql("DROP DATABASE IF EXISTS test_db;");
    sql("CREATE DATABASE test_db;");
    createTestUser();
  }

  static void TearDownTestSuite() {
    switchToAdmin();
    dropTestUser();
    sql("DROP DATABASE IF EXISTS test_db;");
    DBHandlerTestFixture::TearDownTestSuite();
  }

  static void createTestUser() {
    sql("CREATE USER test_user (password = 'test_pass');");
    sql("GRANT ACCESS ON DATABASE test_db TO test_user;");
  }

  static void dropTestUser() { sql("DROP USER IF EXISTS test_user;"); }

  void loginTestUser() { login("test_user", "test_pass", "test_db"); }

  void dropTestTables() {
    sql("DROP TABLE IF EXISTS test_table_1;");
    sql("DROP TABLE IF EXISTS test_table_2;");
    sql("DROP TABLE IF EXISTS test_table_3;");
    sql("DROP TABLE IF EXISTS test_table_4;");
    sql("DROP FOREIGN TABLE IF EXISTS test_foreign_table;");
    sql("DROP TABLE IF EXISTS test_temp_table;");
    sql("DROP TABLE IF EXISTS test_arrow_table;");
    sql("DROP VIEW IF EXISTS test_view;");
  }

  void assertExpectedHeaders(const TQueryResult& result) {
    std::vector<std::string> headers{"table_id",
                                     "table_name",
                                     "column_count",
                                     "is_sharded_table",
                                     "shard_count",
                                     "max_rows",
                                     "fragment_size",
                                     "max_rollback_epochs",
                                     "min_epoch",
                                     "max_epoch",
                                     "min_epoch_floor",
                                     "max_epoch_floor",
                                     "metadata_file_count",
                                     "total_metadata_file_size",
                                     "total_metadata_page_count",
                                     "total_free_metadata_page_count",
                                     "data_file_count",
                                     "total_data_file_size",
                                     "total_data_page_count",
                                     "total_free_data_page_count"};
    if (isDistributedMode()) {
      headers.insert(headers.begin(), "leaf_index");
    }
    for (size_t i = 0; i < headers.size(); i++) {
      EXPECT_EQ(headers[i], result.row_set.row_desc[i].col_name);
    }
  }

  void assertMaxRollbackUpdateResult(int max_rollback_epochs,
                                     int used_metadata_pages,
                                     int used_data_pages,
                                     int epoch,
                                     int epoch_floor) {
    TQueryResult result;
    sql(result, "show table details;");
    assertExpectedHeaders(result);

    // clang-format off
    if (isDistributedMode()) {
      assertResultSetEqual({{i(0), i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(max_rollback_epochs), i(epoch), i(epoch),
                             i(epoch_floor), i(epoch_floor), i(1), i(DEFAULT_METADATA_FILE_SIZE),
                             i(PAGES_PER_METADATA_FILE), i(PAGES_PER_METADATA_FILE - used_metadata_pages),
                             i(1), i(DEFAULT_DATA_FILE_SIZE), i(PAGES_PER_DATA_FILE),
                             i(PAGES_PER_DATA_FILE - used_data_pages)},
                            {i(1), i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(max_rollback_epochs), i(epoch), i(epoch),
                             i(epoch_floor), i(epoch_floor), i(1), i(DEFAULT_METADATA_FILE_SIZE),
                             i(PAGES_PER_METADATA_FILE), i(PAGES_PER_METADATA_FILE - used_metadata_pages),
                             i(1), i(DEFAULT_DATA_FILE_SIZE), i(PAGES_PER_DATA_FILE),
                             i(PAGES_PER_DATA_FILE - used_data_pages)}},
                           result);
    } else {
      assertResultSetEqual({{i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(max_rollback_epochs), i(epoch), i(epoch),
                             i(epoch_floor), i(epoch_floor), i(1), i(DEFAULT_METADATA_FILE_SIZE),
                             i(PAGES_PER_METADATA_FILE), i(PAGES_PER_METADATA_FILE - used_metadata_pages),
                             i(1), i(DEFAULT_DATA_FILE_SIZE), i(PAGES_PER_DATA_FILE),
                             i(PAGES_PER_DATA_FILE - used_data_pages)}},
                           result);
    }
    // clang-format on
  }

  void assertTablesWithContentResult(const TQueryResult result, int64_t data_page_size) {
    int64_t data_file_size;
    if (data_page_size == -1) {
      data_file_size = DEFAULT_PAGE_SIZE * PAGES_PER_DATA_FILE;
    } else {
      data_file_size = data_page_size * PAGES_PER_DATA_FILE;
    }

    // clang-format off
    if (isDistributedMode()) {
      assertResultSetEqual({{i(0), i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                             i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                             i(0)},
                            {i(0), i(2), "test_table_2", i(5), True, i(1), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(1), i(1),
                             i(0), i(0), i(1), i(DEFAULT_METADATA_FILE_SIZE),
                             i(PAGES_PER_METADATA_FILE), i(PAGES_PER_METADATA_FILE - 4), i(1),
                             i(data_file_size), i(PAGES_PER_DATA_FILE), i(PAGES_PER_DATA_FILE - 4)},
                            {i(0), i(4), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(1), i(1),
                             i(0), i(0), i(1), i(DEFAULT_METADATA_FILE_SIZE),
                             i(PAGES_PER_METADATA_FILE), i(PAGES_PER_METADATA_FILE - 2), i(1),
                             i(data_file_size), i(PAGES_PER_DATA_FILE), i(PAGES_PER_DATA_FILE - 2)},
                            {i(1), i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                             i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                             i(0)},
                            {i(1), i(2), "test_table_2", i(5), True, i(1), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(1), i(1),
                             i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0)},
                            {i(1), i(4), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(1), i(1),
                             i(0), i(0), i(1), i(DEFAULT_METADATA_FILE_SIZE),
                             i(PAGES_PER_METADATA_FILE), i(PAGES_PER_METADATA_FILE - 2), i(1),
                             i(data_file_size), i(PAGES_PER_DATA_FILE), i(PAGES_PER_DATA_FILE - 2)}},
                           result);
    } else {
      assertResultSetEqual({{i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(1), i(1),
                             i(0), i(0), i(1), i(DEFAULT_METADATA_FILE_SIZE),
                             i(PAGES_PER_METADATA_FILE), i(PAGES_PER_METADATA_FILE - 3), i(1),
                             i(data_file_size), i(PAGES_PER_DATA_FILE), i(PAGES_PER_DATA_FILE - 3)},
                            {i(2), "test_table_2", i(5), True, i(2), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(1), i(1),
                             i(0), i(0), i(1), i(DEFAULT_METADATA_FILE_SIZE),
                             i(PAGES_PER_METADATA_FILE), i(PAGES_PER_METADATA_FILE - 4), i(1),
                             i(data_file_size), i(PAGES_PER_DATA_FILE), i(PAGES_PER_DATA_FILE - 4)},
                            {i(5), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(1), i(1),
                             i(0), i(0), i(1), i(DEFAULT_METADATA_FILE_SIZE),
                             i(PAGES_PER_METADATA_FILE), i(PAGES_PER_METADATA_FILE - 2), i(1),
                             i(data_file_size), i(PAGES_PER_DATA_FILE), i(PAGES_PER_DATA_FILE - 2)}},
                           result);
    }
    // clang-format on
  }

  // In the case where table page size is set to METADATA_PAGE_SIZE, both
  // the data and metadata content are stored in the data files
  void assertTablesWithContentAndSamePageSizeResult(const TQueryResult result) {
    int64_t data_file_size{METADATA_PAGE_SIZE * PAGES_PER_DATA_FILE};
    // clang-format off
    if (isDistributedMode()) {
      assertResultSetEqual({{i(0), i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                             i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                             i(0)},
                            {i(0), i(2), "test_table_2", i(5), True, i(1), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(1), i(1),
                             i(0), i(0), i(0), i(0), i(0), i(0), i(1), i(data_file_size),
                             i(PAGES_PER_DATA_FILE), i(PAGES_PER_DATA_FILE - 8)},
                            {i(0), i(4), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(1), i(1),
                             i(0), i(0), i(0), i(0), i(0), i(0), i(1), i(data_file_size),
                             i(PAGES_PER_DATA_FILE), i(PAGES_PER_DATA_FILE - 4)},
                            {i(1), i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                             i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                             i(0)},
                            {i(1), i(2), "test_table_2", i(5), True, i(1), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(1), i(1),
                             i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0)},
                            {i(1), i(4), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(1), i(1),
                             i(0), i(0), i(0), i(0), i(0), i(0), i(1), i(data_file_size),
                             i(PAGES_PER_DATA_FILE), i(PAGES_PER_DATA_FILE - 4)}},
                           result);
    } else {
      assertResultSetEqual({{i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(1), i(1),
                             i(0), i(0), i(0), i(0), i(0), i(0), i(1), i(data_file_size),
                             i(PAGES_PER_DATA_FILE), i(PAGES_PER_DATA_FILE - 6)},
                            {i(2), "test_table_2", i(5), True, i(2), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(1), i(1),
                             i(0), i(0), i(0), i(0), i(0), i(0), i(1), i(data_file_size),
                             i(PAGES_PER_DATA_FILE), i(PAGES_PER_DATA_FILE - 8)},
                            {i(5), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                             i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(1), i(1),
                             i(0), i(0), i(0), i(0), i(0), i(0), i(1), i(data_file_size),
                             i(PAGES_PER_DATA_FILE), i(PAGES_PER_DATA_FILE - 4)}},
                           result);
    }
    // clang-format on
  }

  std::string getWithPageSize() {
    std::string with_page_size;
    auto page_size = GetParam();
    if (page_size != -1) {
      with_page_size = " with (page_size = " + std::to_string(page_size) + ")";
    }
    return with_page_size;
  }

  std::string getPageSizeOption() {
    std::string page_size_option;
    auto page_size = GetParam();
    if (page_size != -1) {
      page_size_option = ", page_size = " + std::to_string(page_size);
    }
    return page_size_option;
  }
};

TEST_F(ShowTableDetailsTest, EmptyTables) {
  sql("create table test_table_1 (c1 int, c2 text);");
  sql("create table test_table_2 (c1 int, c2 text, c3 double, shard key(c1)) with "
      "(shard_count = 2, max_rows = 10);");
  sql("create table test_table_3 (c1 int) with (partitions = 'REPLICATED', "
      "fragment_size "
      "= 5);");

  TQueryResult result;
  sql(result, "show table details;");
  assertExpectedHeaders(result);

  // clang-format off
  if (isDistributedMode()) {
    assertResultSetEqual({{i(0), i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)},
                          {i(0), i(2), "test_table_2", i(5), True, i(1), i(10),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)},
                          {i(0), i(4), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(5), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0), i(0), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0)},
                          {i(1), i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)},
                          {i(1), i(2), "test_table_2", i(5), True, i(1), i(10),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)},
                          {i(1), i(4), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(5), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0), i(0), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0)}},
                         result);
  } else {
    assertResultSetEqual({{i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)},
                          {i(2), "test_table_2", i(5), True, i(2), i(10),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)},
                          {i(5), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(5), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0), i(0), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0)}},
                         result);
  }
  // clang-format on
}

TEST_F(ShowTableDetailsTest, NotCaseSensitive) {
  sql("create table TEST_table_1 (c1 int, c2 text);");

  TQueryResult result;
  sql(result, "show table details test_TABLE_1;");
  assertExpectedHeaders(result);

  // clang-format off
  if (isDistributedMode()) {
    assertResultSetEqual({{i(0), i(1), "TEST_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)},
                          {i(1), i(1), "TEST_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)}},
                         result);
  }
  else {
    assertResultSetEqual({{i(1), "TEST_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)}},
                         result);
  }
  // clang-format on
}

TEST_P(ShowTableDetailsTest, TablesWithContent) {
  sql("create table test_table_1 (c1 int, c2 text) " + getWithPageSize() + ";");

  // Inserts for non-sharded tables are non-deterministic in distributed mode
  if (!isDistributedMode()) {
    sql("insert into test_table_1 values (10, 'abc');");
  }

  sql("create table test_table_2 (c1 int, c2 text, c3 double, shard key(c1)) with "
      "(shard_count = 2" +
      getPageSizeOption() + ");");
  sql("insert into test_table_2 values (20, 'efgh', 1.23);");

  sql("create table test_table_3 (c1 int) with (partitions = 'REPLICATED'" +
      getPageSizeOption() + ");");
  sql("insert into test_table_3 values (50);");

  TQueryResult result;
  sql(result, "show table details;");
  assertExpectedHeaders(result);

  auto page_size = GetParam();
  if (page_size == METADATA_PAGE_SIZE) {
    assertTablesWithContentAndSamePageSizeResult(result);
  } else {
    assertTablesWithContentResult(result, page_size);
  }
}

INSTANTIATE_TEST_SUITE_P(
    DifferentPageSizes,
    ShowTableDetailsTest,
    testing::Values(-1 /* Use default */,
                    100 /* Arbitrary page size */,
                    METADATA_PAGE_SIZE,
                    65536 /* Results in the same file size as the metadata file */),
    [](const auto& param_info) {
      auto page_size = param_info.param;
      return "Page_Size_" + (page_size == -1 ? "Default" : std::to_string(page_size));
    });

TEST_F(ShowTableDetailsTest, MaxRollbackEpochsUpdates) {
  // For distributed mode, a replicated table is used in this test case
  // in order to simplify table storage assertions (since all tables
  // will have the same content)
  sql("create table test_table_1 (c1 int, c2 int) with (max_rollback_epochs = 15, "
      "partitions = 'REPLICATED');");
  sql("insert into test_table_1 values (1, 2);");
  sql("insert into test_table_1 values (10, 20);");
  for (int i = 0; i < 2; i++) {
    sql("update test_table_1 set c1 = c1 + 1 where c1 >= 10;");
  }
  assertMaxRollbackUpdateResult(15, 8, 5, 6, 0);

  sql("alter table test_table_1 set max_rollback_epochs = 1;");
  assertMaxRollbackUpdateResult(1, 3, 3, 7, 5);
}

TEST_F(ShowTableDetailsTest, CommandWithTableNames) {
  sql("create table test_table_1 (c1 int, c2 text);");
  sql("create table test_table_2 (c1 int, c2 text, c3 double, shard key(c1)) with "
      "(shard_count = 2);");
  sql("create table test_table_3 (c1 int) with (partitions = 'REPLICATED');");

  TQueryResult result;
  sql(result, "show table details test_table_1, test_table_3;");
  assertExpectedHeaders(result);

  // clang-format off
  if (isDistributedMode()) {
    assertResultSetEqual({{i(0), i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)},
                          {i(0), i(4), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)},
                          {i(1), i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)},
                          {i(1), i(4), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)}},
                         result);
  } else {
    assertResultSetEqual({{i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)},
                          {i(5), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)}},
                         result);
  }
  // clang-format on
}

TEST_F(ShowTableDetailsTest, UserSpecificTables) {
  sql("create table test_table_1 (c1 int, c2 text);");
  sql("create table test_table_2 (c1 int, c2 text, c3 double, shard key(c1)) with "
      "(shard_count = 2);");
  sql("create table test_table_3 (c1 int) with (partitions = 'REPLICATED');");
  sql("GRANT SELECT ON TABLE test_table_3 TO test_user;");

  loginTestUser();

  TQueryResult result;
  sql(result, "show table details;");
  assertExpectedHeaders(result);

  // clang-format off
  if (isDistributedMode()) {
    assertResultSetEqual({{i(0), i(4), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)},
                          {i(1), i(4), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)}},
                         result);
  } else {
    assertResultSetEqual({{i(5), "test_table_3", i(3), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)}},
                         result);
  }
  // clang-format on
}

TEST_F(ShowTableDetailsTest, InaccessibleTable) {
  sql("create table test_table_1 (c1 int, c2 text);");
  sql("create table test_table_2 (c1 int, c2 text, c3 double, shard key(c1)) with "
      "(shard_count = 2);");
  sql("create table test_table_3 (c1 int) with (partitions = 'REPLICATED');");

  loginTestUser();
  queryAndAssertException("show table details test_table_1;",
                          "Unable to show table details for table: "
                          "test_table_1. Table does not exist.");
}

TEST_F(ShowTableDetailsTest, NonExistentTable) {
  sql("create table test_table_1 (c1 int, c2 text);");
  sql("create table test_table_2 (c1 int, c2 text, c3 double, shard key(c1)) with "
      "(shard_count = 2);");
  sql("create table test_table_3 (c1 int) with (partitions = 'REPLICATED');");

  queryAndAssertException("show table details test_table_4;",
                          "Unable to show table details for table: "
                          "test_table_4. Table does not exist.");
}

TEST_F(ShowTableDetailsTest, UnsupportedTableTypes) {
  sql("create table test_table_1 (c1 int, c2 text);");
  sql("create temporary table test_temp_table (c1 int, c2 text);");
  sql("create dataframe test_arrow_table (c1 int) from 'CSV:" +
      boost::filesystem::canonical("../../Tests/FsiDataFiles/0.csv").string() + "';");
  sql("create view test_view as select * from test_table_1;");

  if (!isDistributedMode()) {
    sql("CREATE FOREIGN TABLE test_foreign_table(i INTEGER) SERVER omnisci_local_csv "
        "WITH "
        "(file_path = '" +
        boost::filesystem::canonical("../../Tests/FsiDataFiles/0.csv").string() + "');");
  }

  TQueryResult result;
  sql(result, "show table details;");
  assertExpectedHeaders(result);

  // clang-format off
  if (isDistributedMode()) {
    assertResultSetEqual({{i(0), i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)},
                          {i(1), i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)}},
                         result);
  } else {
    assertResultSetEqual({{i(1), "test_table_1", i(4), False, i(0), i(DEFAULT_MAX_ROWS),
                           i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0), i(0),
                           i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0), i(0),
                           i(0)}},
                         result);
  }
  // clang-format on
}

TEST_F(ShowTableDetailsTest, FsiTableSpecified) {
  if (isDistributedMode()) {
    GTEST_SKIP() << "Foreign tables are currently not supported in distributed mode";
  }

  sql("CREATE FOREIGN TABLE test_foreign_table(i INTEGER) SERVER omnisci_local_csv "
      "WITH "
      "(file_path = '" +
      boost::filesystem::canonical("../../Tests/FsiDataFiles/0.csv").string() + "');");

  queryAndAssertException("show table details test_foreign_table;",
                          "SHOW TABLE DETAILS is not supported for foreign "
                          "tables. Table name: test_foreign_table.");
}

TEST_F(ShowTableDetailsTest, TemporaryTableSpecified) {
  sql("create temporary table test_temp_table (c1 int, c2 text);");

  queryAndAssertException("show table details test_temp_table;",
                          "SHOW TABLE DETAILS is not supported for temporary "
                          "tables. Table name: test_temp_table.");
}

TEST_F(ShowTableDetailsTest, ArrowFsiTableSpecified) {
  sql("create dataframe test_arrow_table (c1 int) from 'CSV:" +
      boost::filesystem::canonical("../../Tests/FsiDataFiles/0.csv").string() + "';");

  queryAndAssertException("show table details test_arrow_table;",
                          "SHOW TABLE DETAILS is not supported for temporary "
                          "tables. Table name: test_arrow_table.");
}

TEST_F(ShowTableDetailsTest, ViewSpecified) {
  sql("create table test_table_1 (c1 int, c2 text);");
  sql("create view test_view as select * from test_table_1;");

  queryAndAssertException("show table details test_view;",
                          "Unable to show table details for table: "
                          "test_view. Table does not exist.");
}

class ShowQueriesTest : public DBHandlerTestFixture {
 public:
  static void SetUpTestSuite() {
    createDBHandler();
    createTestUser();
    loginAdmin();
  }

  static void TearDownTestSuite() {
    switchToAdmin();
    sql("DROP TABLE IF EXISTS test_table;");
    dropTestUser();
  }

  void SetUp() override { DBHandlerTestFixture::SetUp(); }

  void TearDown() override { DBHandlerTestFixture::TearDown(); }

  static void createTestUser() {
    sql("DROP USER IF EXISTS u1;");
    sql("DROP USER IF EXISTS u2;");
    sql("CREATE USER u1 (password = 'u1');");
    sql("GRANT ALL ON DATABASE omnisci TO u1;");
    sql("CREATE USER u2 (password = 'u2');");
    sql("GRANT ALL ON DATABASE omnisci TO u2;");
  }

  static void dropTestUser() {
    sql("DROP USER IF EXISTS u1;");
    sql("DROP USER IF EXISTS u2;");
  }
};

TEST_F(ShowQueriesTest, NonAdminUser) {
  TQueryResult non_admin_res, admin_res, own_res, res1;
  TSessionId query_session;
  TSessionId show_queries_cmd_session;

  login("u1", "u1", "omnisci", query_session);
  auto executor = Executor::getExecutor(Executor::UNITARY_EXECUTOR_ID);
  // mock the running query by just enrolling the meaningless query
  executor->enrollQuerySession(
      query_session, "MOCK_QUERY", "0", 0, QuerySessionStatus::RUNNING_QUERY_KERNEL);

  auto show_queries_thread1 = std::async(std::launch::deferred, [&] {
    login("u2", "u2", "omnisci", show_queries_cmd_session);
    sql(non_admin_res, "SHOW QUERIES;", show_queries_cmd_session);
  });

  auto show_queries_thread2 = std::async(std::launch::deferred, [&] {
    switchToAdmin();
    auto p = getDbHandlerAndSessionId();
    sql(admin_res, "SHOW QUERIES;", p.second);
  });

  auto show_queries_thread3 = std::async(
      std::launch::deferred, [&] { sql(own_res, "SHOW QUERIES;", query_session); });

  show_queries_thread1.get();
  show_queries_thread2.get();
  show_queries_thread3.get();

  EXPECT_TRUE(query_session.compare(show_queries_cmd_session) != 0);
  // non-admin && non-own session cannot see the query status
  EXPECT_TRUE(non_admin_res.row_set.columns[0].data.str_col.empty());
  // admin && own user can see the query status
  EXPECT_TRUE(!admin_res.row_set.columns[0].data.str_col.empty());
  EXPECT_TRUE(!own_res.row_set.columns[0].data.str_col.empty());
  {
    mapd_unique_lock<mapd_shared_mutex> session_write_lock(executor->getSessionLock());
    executor->removeFromQuerySessionList(query_session, "0", session_write_lock);
  }
}

class SystemTablesTest : public DBHandlerTestFixture {
 protected:
  static void SetUpTestSuite() {
    if (!isDistributedMode()) {
      DBHandlerTestFixture::SetUpTestSuite();
      switchToAdmin();
      createUser("test_user_1");
      createUser("test_user_2");
    }
  }

  static void TearDownTestSuite() {
    if (!isDistributedMode()) {
      switchToAdmin();
      dropUser("test_user_1");
      dropUser("test_user_2");
      DBHandlerTestFixture::TearDownTestSuite();
    }
  }

  void SetUp() override {
    if (isDistributedMode()) {
      GTEST_SKIP() << "Test is not supported in distributed mode.";
    }
    resetDbObjectsAndPermissions();
    loginInformationSchema();
    g_enable_system_tables = true;
  }

  void TearDown() override {
    if (isDistributedMode()) {
      GTEST_SKIP() << "Test is not supported in distributed mode.";
    }
    switchToAdmin();
    sql("DROP TABLE IF EXISTS test_table_1;");
    dropUser("test_user_3");
    resetDbObjectsAndPermissions();
  }

  void resetDbObjectsAndPermissions() {
    switchToAdmin();
    dropDatabases();
    dropRoles();
    resetPermissions();
  }

  static void createUser(const std::string& user_name) {
    sql("CREATE USER " + user_name +
        " (password = 'test_pass', is_super = 'false', default_db = 'omnisci');");
    sql("GRANT ACCESS ON DATABASE information_schema TO " + user_name + ";");
  }

  static void dropUser(const std::string& user_name) {
    switchToAdmin();
    sql("DROP USER IF EXISTS " + user_name + ";");
  }

  int64_t getUserId(const std::string& user_name) {
    auto& system_catalog = Catalog_Namespace::SysCatalog::instance();
    Catalog_Namespace::UserMetadata user;
    CHECK(system_catalog.getMetadataForUser(user_name, user));
    return user.userId;
  }

  int64_t getTableId(const std::string& table_name) {
    auto td = getCatalog().getMetadataForTable(table_name, false);
    CHECK(td);
    return td->tableId;
  }

  void createDashboard(const std::string& dashboard_name) {
    const auto& [db_handler, session_id] = getDbHandlerAndSessionId();
    auto id = db_handler->create_dashboard(
        session_id, dashboard_name, "state", "image", "\"table\":\"test_table\"");
    dashboard_id_by_name_[dashboard_name] = id;
  }

  void updateDashboardName(const std::string& old_name, const std::string& new_name) {
    const auto& [db_handler, session_id] = getDbHandlerAndSessionId();
    CHECK(dashboard_id_by_name_.find(old_name) != dashboard_id_by_name_.end());
    auto id = dashboard_id_by_name_[old_name];
    auto dashboard = getCatalog().getMetadataForDashboard(id);
    CHECK(dashboard);
    db_handler->replace_dashboard(session_id,
                                  id,
                                  new_name,
                                  "admin",
                                  dashboard->dashboardState,
                                  dashboard->imageHash,
                                  dashboard->dashboardMetadata);
    CHECK(dashboard_id_by_name_.find(old_name) != dashboard_id_by_name_.end());
    dashboard_id_by_name_[new_name] = dashboard_id_by_name_[old_name];
    dashboard_id_by_name_.erase(old_name);
  }

  std::string getLastUpdatedTime(const std::string& dashboard_name) {
    CHECK(dashboard_id_by_name_.find(dashboard_name) != dashboard_id_by_name_.end());
    auto dashboard =
        getCatalog().getMetadataForDashboard(dashboard_id_by_name_[dashboard_name]);
    CHECK(dashboard);
    return dashboard->updateTime;
  }

  void dropDatabases() {
    sql("DROP DATABASE IF EXISTS test_db_1;");
    sql("DROP DATABASE IF EXISTS test_db_2;");
  }

  int64_t getDbId(const std::string& db_name) {
    auto& system_catalog = Catalog_Namespace::SysCatalog::instance();
    Catalog_Namespace::DBMetadata db_metadata;
    CHECK(system_catalog.getMetadataForDB(db_name, db_metadata));
    return db_metadata.dbId;
  }

  void dropRoles() {
    sql("DROP ROLE IF EXISTS test_role_1;");
    sql("DROP ROLE IF EXISTS test_role_2;");
  }

  void resetPermissions() {
    sql("REVOKE ALL ON DATABASE omnisci FROM test_user_1, test_user_2;");
    sql("REVOKE ALL ON DATABASE information_schema FROM test_user_1, test_user_2;");
    sql("GRANT ACCESS ON DATABASE information_schema TO test_user_1, test_user_2;");
  }

  void loginInformationSchema() {
    login("admin", "HyperInteractive", "information_schema");
  }

  void initTestTableAndClearMemory() {
    switchToAdmin();
    sql("CREATE TABLE test_table_1 (i INTEGER);");
    sql("INSERT INTO test_table_1 VALUES (10);");
    sql("SELECT * FROM test_table_1;");
    sql("ALTER SYSTEM CLEAR CPU MEMORY;");
    sql("ALTER SYSTEM CLEAR GPU MEMORY;");
  }

  Buffer_Namespace::CpuBufferMgr* getCpuBufferMgr() {
    auto cpu_buffer_mgr = getCatalog().getDataMgr().getCpuBufferMgr();
    CHECK(cpu_buffer_mgr);
    return cpu_buffer_mgr;
  }

  int64_t getCpuPageSize() { return getCpuBufferMgr()->getPageSize(); }

  int64_t getMaxCpuPageCount() {
    return getCpuBufferMgr()->getMaxBufferSize() / getCpuPageSize();
  }

  int64_t getAllocatedCpuPageCount() {
    return getTotalSlabPages(getCpuBufferMgr()->getSlabSegments());
  }

  Buffer_Namespace::GpuCudaBufferMgr* getGpuBufferMgr() {
    auto gpu_buffer_mgr = getCatalog().getDataMgr().getGpuBufferMgr();
    CHECK(gpu_buffer_mgr);
    return gpu_buffer_mgr;
  }

  int64_t getGpuPageSize() { return getGpuBufferMgr()->getPageSize(); }

  int64_t getMaxGpuPageCount() {
    return getGpuBufferMgr()->getMaxBufferSize() / getGpuPageSize();
  }

  int64_t getAllocatedGpuPageCount() {
    return getTotalSlabPages(getGpuBufferMgr()->getSlabSegments());
  }

  int64_t getTotalSlabPages(
      const std::vector<Buffer_Namespace::BufferList>& slab_segments_vector) {
    int64_t pages_count{0};
    for (const auto& slab_segments : slab_segments_vector) {
      for (const auto& segment : slab_segments) {
        pages_count += segment.num_pages;
      }
    }
    return pages_count;
  }

  std::map<std::string, int32_t> dashboard_id_by_name_;
};

TEST_F(SystemTablesTest, SuperUser) {
  sqlAndCompareResult("SELECT COUNT(*) FROM users;", {{i(3)}});
}

TEST_F(SystemTablesTest, UserWithTableAccess) {
  switchToAdmin();
  sql("GRANT SELECT ON DATABASE information_schema TO test_user_1;");

  login("test_user_1", "test_pass", "information_schema");
  sqlAndCompareResult("SELECT COUNT(*) FROM users;", {{i(3)}});
}

TEST_F(SystemTablesTest, UserWithoutTableAccess) {
  login("test_user_2", "test_pass", "information_schema");
  queryAndAssertException("SELECT COUNT(*) FROM users;",
                          "Violation of access privileges: user test_user_2 has no "
                          "proper privileges for object users");
}

TEST_F(SystemTablesTest, DatabaseObjectUpdates) {
  queryAndAssertException(
      "DELETE FROM users WHERE user_name = 'test_user_1';",
      "Write requests/queries are not allowed in the information_schema database.");
  queryAndAssertException(
      "INSERT INTO users VALUES (10, 'test_user_3', false, 1, true);",
      "Write requests/queries are not allowed in the information_schema database.");
  queryAndAssertException(
      "TRUNCATE TABLE users;",
      "Write requests/queries are not allowed in the information_schema database.");
  queryAndAssertException(
      "UPDATE users SET user_name = 'test_user_3' WHERE user_name = 'test_user_1';",
      "Write requests/queries are not allowed in the information_schema database.");
  queryAndAssertException(
      "OPTIMIZE TABLE users;",
      "Write requests/queries are not allowed in the information_schema database.");
  queryAndAssertException(
      "DROP TABLE users;",
      "Write requests/queries are not allowed in the information_schema database.");
  queryAndAssertException(
      "ALTER TABLE users RENAME TO users2;",
      "Write requests/queries are not allowed in the information_schema database.");
  queryAndAssertException("DUMP TABLE users TO 'test_path';",
                          "Dumping a system table is not supported.");
  queryAndAssertException(
      "RESTORE TABLE test_table FROM 'test_path';",
      "Write requests/queries are not allowed in the information_schema database.");
  queryAndAssertException(
      "COPY users FROM 'test_path';",
      "Write requests/queries are not allowed in the information_schema database.");
  queryAndAssertException(
      "CREATE TABLE test_table (i INTEGER);",
      "Write requests/queries are not allowed in the information_schema database.");
  queryAndAssertException(
      "CREATE TABLE test_table AS (SELECT * FROM users);",
      "Write requests/queries are not allowed in the information_schema database.");
  queryAndAssertException(
      "INSERT INTO users SELECT * FROM users;",
      "Write requests/queries are not allowed in the information_schema database.");
  queryAndAssertException(
      "CREATE VIEW test_view AS SELECT * FROM users;",
      "Write requests/queries are not allowed in the information_schema database.");
  executeLambdaAndAssertException(
      [this]() { createDashboard("test_dashboard_1"); },
      "Write requests/queries are not allowed in the information_schema database.");
}

TEST_F(SystemTablesTest, DropInformationSchemaDb) {
  queryAndAssertException(
      "DROP DATABASE information_schema;",
      "Write requests/queries are not allowed in the information_schema database.");
}

TEST_F(SystemTablesTest, SystemTableDisabled) {
  g_enable_system_tables = false;
  queryAndAssertException(
      "SELECT COUNT(*) FROM users;",
      "Query cannot be executed because use of system tables is currently disabled.");
}

TEST_F(SystemTablesTest, UsersSystemTable) {
  sqlAndCompareResult("SELECT * FROM users ORDER BY user_name;",
                      {{getUserId("admin"), "admin", True, i(-1), True},
                       {getUserId("test_user_1"), "test_user_1", False, i(1), True},
                       {getUserId("test_user_2"), "test_user_2", False, i(1), True}});

  switchToAdmin();
  createUser("test_user_3");

  loginInformationSchema();
  sqlAndCompareResult("SELECT * FROM users ORDER BY user_name;",
                      {{getUserId("admin"), "admin", True, i(-1), True},
                       {getUserId("test_user_1"), "test_user_1", False, i(1), True},
                       {getUserId("test_user_2"), "test_user_2", False, i(1), True},
                       {getUserId("test_user_3"), "test_user_3", False, i(1), True}});

  switchToAdmin();
  sql("ALTER USER test_user_3 (is_super = 'true');");

  loginInformationSchema();
  sqlAndCompareResult("SELECT * FROM users ORDER BY user_name;",
                      {{getUserId("admin"), "admin", True, i(-1), True},
                       {getUserId("test_user_1"), "test_user_1", False, i(1), True},
                       {getUserId("test_user_2"), "test_user_2", False, i(1), True},
                       {getUserId("test_user_3"), "test_user_3", True, i(1), True}});

  switchToAdmin();
  dropUser("test_user_3");

  loginInformationSchema();
  sqlAndCompareResult("SELECT * FROM users ORDER BY user_name;",
                      {{getUserId("admin"), "admin", True, i(-1), True},
                       {getUserId("test_user_1"), "test_user_1", False, i(1), True},
                       {getUserId("test_user_2"), "test_user_2", False, i(1), True}});
}

TEST_F(SystemTablesTest, TablesSystemTable) {
  switchToAdmin();
  sql("CREATE DATABASE test_db_1;");

  login("admin", "HyperInteractive", "test_db_1");
  sql("CREATE TABLE test_table_1 (i INTEGER);");
  sql("CREATE VIEW test_view_1 AS SELECT * FROM test_table_1;");

  loginInformationSchema();
  // Skip the "omnisci" database, since it can contain default created tables
  // and tables created by other test suites.
  // clang-format off
  sqlAndCompareResult("SELECT * FROM tables WHERE database_id <> " +
                      std::to_string(getDbId("omnisci")) + " ORDER BY table_name;",
                      {{i(3), i(1), "test_table_1", getUserId("admin"), i(3), False, Null,
                        i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_CHUNK_SIZE), i(DEFAULT_PAGE_SIZE),
                        i(DEFAULT_MAX_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0)},
                       {i(3), i(2), "test_view_1", getUserId("admin"), i(2), True,
                        "SELECT * FROM test_table_1;", i(DEFAULT_FRAGMENT_ROWS),
                        i(DEFAULT_MAX_CHUNK_SIZE), i(DEFAULT_PAGE_SIZE), i(DEFAULT_MAX_ROWS),
                        i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0)}});
  // clang-format on

  login("admin", "HyperInteractive", "test_db_1");
  sql("ALTER TABLE test_table_1 RENAME TO test_table_2;");
  sql("CREATE VIEW test_view_2 AS SELECT * FROM test_table_2;");

  loginInformationSchema();
  // clang-format off
  sqlAndCompareResult("SELECT * FROM tables WHERE database_id <> " +
                      std::to_string(getDbId("omnisci")) + " ORDER BY table_name;",
                      {{i(3), i(1), "test_table_2", getUserId("admin"), i(3), False, Null,
                        i(DEFAULT_FRAGMENT_ROWS), i(DEFAULT_MAX_CHUNK_SIZE), i(DEFAULT_PAGE_SIZE),
                        i(DEFAULT_MAX_ROWS), i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0)},
                       {i(3), i(2), "test_view_1", getUserId("admin"), i(2), True,
                        "SELECT * FROM test_table_1;", i(DEFAULT_FRAGMENT_ROWS),
                        i(DEFAULT_MAX_CHUNK_SIZE), i(DEFAULT_PAGE_SIZE), i(DEFAULT_MAX_ROWS),
                        i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0)},
                       {i(3), i(3), "test_view_2", getUserId("admin"), i(2), True,
                        "SELECT * FROM test_table_2;", i(DEFAULT_FRAGMENT_ROWS),
                        i(DEFAULT_MAX_CHUNK_SIZE), i(DEFAULT_PAGE_SIZE), i(DEFAULT_MAX_ROWS),
                        i(DEFAULT_MAX_ROLLBACK_EPOCHS), i(0)}});
  // clang-format on
}

TEST_F(SystemTablesTest, DashboardsSystemTable) {
  switchToAdmin();
  sql("CREATE DATABASE test_db_1;");

  login("admin", "HyperInteractive", "test_db_1");
  createDashboard("test_dashboard_1");
  auto last_updated_1 = getLastUpdatedTime("test_dashboard_1");

  loginInformationSchema();
  // Skip the "omnisci" database, since it can contain dashboards created by other test
  // suites.
  // clang-format off
  sqlAndCompareResult("SELECT * FROM dashboards WHERE database_id <> " +
                      std::to_string(getDbId("omnisci")) + " ORDER BY dashboard_name;",
                      {{i(3), i(1), "test_dashboard_1", getUserId("admin"), last_updated_1}});
  // clang-format on

  login("admin", "HyperInteractive", "test_db_1");
  createDashboard("test_dashboard_2");
  updateDashboardName("test_dashboard_1", "test_dashboard_3");
  auto last_updated_2 = getLastUpdatedTime("test_dashboard_2");
  auto last_updated_3 = getLastUpdatedTime("test_dashboard_3");

  loginInformationSchema();
  // clang-format off
  sqlAndCompareResult("SELECT * FROM dashboards WHERE database_id <> " +
                      std::to_string(getDbId("omnisci")) + " ORDER BY dashboard_name;",
                      {{i(3), i(2), "test_dashboard_2", getUserId("admin"), last_updated_2},
                       {i(3), i(1), "test_dashboard_3", getUserId("admin"), last_updated_3}});
  // clang-format on
}

TEST_F(SystemTablesTest, DatabasesSystemTable) {
  switchToAdmin();
  sql("CREATE DATABASE test_db_1;");

  loginInformationSchema();
  sqlAndCompareResult(
      "SELECT * FROM databases ORDER BY database_name;",
      {{getDbId("information_schema"), "information_schema", getUserId("admin")},
       {getDbId("omnisci"), "omnisci", getUserId("admin")},
       {getDbId("test_db_1"), "test_db_1", getUserId("admin")}});

  switchToAdmin();
  sql("CREATE DATABASE test_db_2;");

  loginInformationSchema();
  sqlAndCompareResult(
      "SELECT * FROM databases ORDER BY database_name;",
      {{getDbId("information_schema"), "information_schema", getUserId("admin")},
       {getDbId("omnisci"), "omnisci", getUserId("admin")},
       {getDbId("test_db_1"), "test_db_1", getUserId("admin")},
       {getDbId("test_db_2"), "test_db_2", getUserId("admin")}});

  switchToAdmin();
  sql("DROP DATABASE test_db_1;");

  loginInformationSchema();
  sqlAndCompareResult(
      "SELECT * FROM databases ORDER BY database_name;",
      {{getDbId("information_schema"), "information_schema", getUserId("admin")},
       {getDbId("omnisci"), "omnisci", getUserId("admin")},
       {getDbId("test_db_2"), "test_db_2", getUserId("admin")}});
}

TEST_F(SystemTablesTest, PermissionsSystemTable) {
  // clang-format off
  sqlAndCompareResult("SELECT * FROM permissions ORDER BY role_name;",
                      {{"test_user_1", True, i(2), "information_schema", i(-1),
                        getUserId("admin"), "database", array({"access"})},
                       {"test_user_2", True, i(2), "information_schema", i(-1),
                        getUserId("admin"), "database", array({"access"})}});
  // clang-format on

  switchToAdmin();
  sql("GRANT CREATE, SELECT ON DATABASE omnisci to test_user_1;");

  loginInformationSchema();
  // clang-format off
  sqlAndCompareResult("SELECT * FROM permissions ORDER BY role_name, object_name;",
                      {{"test_user_1", True, i(2), "information_schema", i(-1),
                        getUserId("admin"), "database", array({"access"})},
                       {"test_user_1", True, i(1), "omnisci", i(-1), getUserId("admin"),
                        "table", array({"select table", "create table"})},
                       {"test_user_2", True, i(2), "information_schema", i(-1),
                        getUserId("admin"), "database", array({"access"})}});
  // clang-format on
}

TEST_F(SystemTablesTest, RoleAssignmentsSystemTable) {
  switchToAdmin();
  sql("CREATE ROLE test_role_1;");
  sql("GRANT test_role_1 TO test_user_1, test_user_2;");

  loginInformationSchema();
  sqlAndCompareResult("SELECT * FROM role_assignments ORDER BY user_name;",
                      {{"test_role_1", "admin"},
                       {"test_role_1", "test_user_1"},
                       {"test_role_1", "test_user_2"}});

  switchToAdmin();
  sql("REVOKE test_role_1 FROM test_user_1;");

  loginInformationSchema();
  sqlAndCompareResult("SELECT * FROM role_assignments ORDER BY role_name, user_name;",
                      {{"test_role_1", "admin"}, {"test_role_1", "test_user_2"}});
}

TEST_F(SystemTablesTest, RolesSystemTable) {
  switchToAdmin();
  sql("CREATE ROLE test_role_1;");

  loginInformationSchema();
  sqlAndCompareResult("SELECT * FROM roles ORDER BY role_name;", {{"test_role_1"}});

  switchToAdmin();
  sql("CREATE ROLE test_role_2;");

  loginInformationSchema();
  sqlAndCompareResult("SELECT * FROM roles ORDER BY role_name;",
                      {{"test_role_1"}, {"test_role_2"}});

  switchToAdmin();
  sql("DROP ROLE test_role_1;");

  loginInformationSchema();
  sqlAndCompareResult("SELECT * FROM roles ORDER BY role_name;", {{"test_role_2"}});
}

TEST_F(SystemTablesTest, MemorySummarySystemTableCpu) {
  initTestTableAndClearMemory();

  loginInformationSchema();
  // clang-format off
  sqlAndCompareResult("SELECT * FROM memory_summary WHERE device_type = 'CPU';",
                      {{"Server", i(0), "CPU", getMaxCpuPageCount(), getCpuPageSize(),
                        i(0), i(0), i(0)}});
  // clang-format on

  switchToAdmin();
  sql("ALTER SYSTEM CLEAR CPU MEMORY;");
  sql("SELECT * FROM test_table_1;");

  loginInformationSchema();
  // clang-format off
  sqlAndCompareResult("SELECT * FROM memory_summary WHERE device_type = 'CPU';",
                      {{"Server", i(0), "CPU", getMaxCpuPageCount(), getCpuPageSize(),
                        getAllocatedCpuPageCount(), i(1), getAllocatedCpuPageCount() - 1}});
  // clang-format on
}

TEST_F(SystemTablesTest, MemorySummarySystemTableGpu) {
  if (!setExecuteMode(TExecuteMode::GPU)) {
    GTEST_SKIP() << "GPU is not enabled.";
  }
  initTestTableAndClearMemory();

  sql("ALTER SYSTEM CLEAR GPU MEMORY;");
  sql("SELECT AVG(i) FROM test_table_1;");

  loginInformationSchema();
  // clang-format off
  sqlAndCompareResult(
      "SELECT * FROM memory_summary WHERE device_type = 'GPU' AND device_id = 0;",
      {{"Server", i(0), "GPU", getMaxGpuPageCount(), getGpuPageSize(),
        getAllocatedGpuPageCount(), i(1), getAllocatedGpuPageCount() - 1}});
  // clang-format on
}

TEST_F(SystemTablesTest, MemoryDetailsSystemTableCpu) {
  initTestTableAndClearMemory();

  auto db_id = getDbId("omnisci");
  auto table_id = getTableId("test_table_1");

  loginInformationSchema();
  sqlAndCompareResult("SELECT * FROM memory_details;", {});

  switchToAdmin();
  sql("ALTER SYSTEM CLEAR CPU MEMORY;");
  sql("SELECT * FROM test_table_1;");

  loginInformationSchema();
  // clang-format off
  sqlAndCompareResult("SELECT * FROM memory_details WHERE device_type = 'CPU';",
                      {{"Server", db_id, table_id, i(1), array({db_id, table_id, i(1), i(0)}),
                        i(0), "CPU", "USED", i(1), getCpuPageSize(), i(0), i(0), i(1)},
                       {"Server", Null, Null, Null, Null,
                        i(0), "CPU", "FREE", getAllocatedCpuPageCount() - 1, getCpuPageSize(), i(0), i(1), i(0)}});
  // clang-format on
}

TEST_F(SystemTablesTest, MemoryDetailsSystemTableGpu) {
  if (!setExecuteMode(TExecuteMode::GPU)) {
    GTEST_SKIP() << "GPU is not enabled.";
  }
  initTestTableAndClearMemory();

  auto db_id = getDbId("omnisci");
  auto table_id = getTableId("test_table_1");

  sql("ALTER SYSTEM CLEAR GPU MEMORY;");
  sql("SELECT AVG(i) FROM test_table_1;");

  loginInformationSchema();
  // clang-format off
  sqlAndCompareResult("SELECT * FROM memory_details WHERE device_type = 'GPU' AND device_id = 0;",
                      {{"Server", db_id, table_id, i(1), array({db_id, table_id, i(1), i(0)}),
                        i(0), "GPU", "USED", i(1), getGpuPageSize(), i(0), i(0), i(0)},
                       {"Server", Null, Null, Null, Null,
                        i(0), "GPU", "FREE", getAllocatedGpuPageCount() - 1, getGpuPageSize(), i(0), i(1), i(14)}});
  // clang-format on
}

struct StorageDetailsResult {
  std::string node{"Server"};
  int64_t database_id{0};
  int64_t table_id{0};
  int64_t epoch{1};
  int64_t epoch_floor{0};
  int64_t fragment_count{1};
  int64_t shard_id{-1};
  int64_t data_file_count{1};
  int64_t metadata_file_count{1};
  int64_t total_data_file_size{DEFAULT_DATA_FILE_SIZE};
  int64_t total_data_page_count{PAGES_PER_DATA_FILE};
  int64_t total_free_data_page_count{PAGES_PER_DATA_FILE};
  int64_t total_metadata_file_size{DEFAULT_METADATA_FILE_SIZE};
  int64_t total_metadata_page_count{PAGES_PER_METADATA_FILE};
  int64_t total_free_metadata_page_count{PAGES_PER_METADATA_FILE};
  int64_t total_dictionary_data_file_size{0};
};

class StorageDetailsSystemTableTest : public SystemTablesTest,
                                      public testing::WithParamInterface<int32_t> {
 protected:
  void SetUp() override {
    SystemTablesTest::SetUp();
    switchToAdmin();
    sql("CREATE DATABASE test_db;");
    login("admin", "HyperInteractive", "test_db");
  }

  void TearDown() override {
    switchToAdmin();
    sql("DROP DATABASE IF EXISTS test_db");
    SystemTablesTest::TearDown();
  }

  void sqlAndCompareResult(const std::vector<StorageDetailsResult>& results) {
    std::vector<std::vector<NullableTargetValue>> target_values;
    for (const auto& result : results) {
      target_values.emplace_back(
          std::vector<NullableTargetValue>{result.node,
                                           result.database_id,
                                           result.table_id,
                                           result.epoch,
                                           result.epoch_floor,
                                           result.fragment_count,
                                           result.shard_id,
                                           result.data_file_count,
                                           result.metadata_file_count,
                                           result.total_data_file_size,
                                           result.total_data_page_count,
                                           result.total_free_data_page_count,
                                           result.total_metadata_file_size,
                                           result.total_metadata_page_count,
                                           result.total_free_metadata_page_count,
                                           result.total_dictionary_data_file_size});
    }
    loginInformationSchema();
    // Skip the "omnisci" database, since it can contain default created tables
    // and tables created by other test suites.
    std::string query{"SELECT * FROM storage_details WHERE database_id <> " +
                      std::to_string(getDbId("omnisci")) + " ORDER BY table_id;"};
    SystemTablesTest::sqlAndCompareResult(query, target_values);
  }

  size_t getDictionarySize(const std::string& table_name,
                           const std::string& column_name) {
    const auto& catalog = getCatalog();
    auto td = catalog.getMetadataForTable(table_name, false);
    CHECK(td);
    auto cd = catalog.getMetadataForColumn(td->tableId, column_name);
    CHECK(cd);
    CHECK(cd->columnType.is_dict_encoded_string());
    auto dd = catalog.getMetadataForDict(cd->columnType.get_comp_param(), false);
    CHECK(dd);
    auto& path = dd->dictFolderPath;
    CHECK(boost::filesystem::exists(path));
    CHECK(boost::filesystem::is_directory(path));
    size_t dictionary_size{0};
    for (const auto& entry : boost::filesystem::directory_iterator(path)) {
      CHECK(boost::filesystem::is_regular_file(entry.path()));
      dictionary_size += boost::filesystem::file_size(entry.path());
    }
    return dictionary_size;
  }
};

TEST_F(StorageDetailsSystemTableTest, ShardedTable) {
  sql("CREATE TABLE test_table (c1 INTEGER, c2 TEXT, c3 DOUBLE, SHARD KEY(c1)) WITH "
      "(shard_count = 2);");
  sql("INSERT INTO test_table VALUES (20, 'efgh', 1.23);");

  auto db_id = getDbId("test_db");
  auto table_id = getTableId("test_table");
  StorageDetailsResult shard_1_result;
  shard_1_result.database_id = db_id;
  shard_1_result.table_id = table_id;
  shard_1_result.shard_id = 0;
  // One page for each of the 3 defined columns + the $deleted$ column
  shard_1_result.total_free_metadata_page_count -= 4;
  shard_1_result.total_free_data_page_count -= 4;
  shard_1_result.total_dictionary_data_file_size = getDictionarySize("test_table", "c2");

  StorageDetailsResult shard_2_result;
  shard_2_result.database_id = db_id;
  shard_2_result.table_id = table_id;
  // Only the first shard should contain table data/metadata
  shard_2_result.fragment_count = 0;
  shard_2_result.data_file_count = 0;
  shard_2_result.metadata_file_count = 0;
  shard_2_result.total_data_file_size = 0;
  shard_2_result.total_metadata_file_size = 0;
  shard_2_result.shard_id = 1;
  shard_2_result.total_metadata_page_count = 0;
  shard_2_result.total_free_metadata_page_count = 0;
  shard_2_result.total_data_page_count = 0;
  shard_2_result.total_free_data_page_count = 0;
  shard_2_result.total_dictionary_data_file_size = getDictionarySize("test_table", "c2");
  sqlAndCompareResult({shard_1_result, shard_2_result});
}

TEST_F(StorageDetailsSystemTableTest, MultipleFragments) {
  sql("CREATE TABLE test_table (c1 INTEGER) WITH (fragment_size = 1);");
  const size_t row_count{5};
  for (size_t i = 0; i < row_count; i++) {
    sql("INSERT INTO test_table VALUES (" + std::to_string(i) + ");");
  }

  auto db_id = getDbId("test_db");
  auto table_id = getTableId("test_table");
  StorageDetailsResult result;
  result.database_id = db_id;
  result.table_id = table_id;
  // One page for each defined integer column chunk + the $deleted$ column chunks
  result.total_free_metadata_page_count -= row_count * 2;
  result.total_free_data_page_count -= row_count * 2;
  result.epoch = row_count;
  result.epoch_floor = row_count - DEFAULT_MAX_ROLLBACK_EPOCHS;
  result.fragment_count = row_count;
  sqlAndCompareResult({result});
}

TEST_F(StorageDetailsSystemTableTest, NonLocalTables) {
  sql("CREATE TEMPORARY TABLE test_table (c1 INTEGER);");
  sql("INSERT INTO test_table VALUES (10);");

  sql("CREATE FOREIGN TABLE test_foreign_table (i INTEGER) SERVER omnisci_local_csv WITH "
      "(file_path = '" +
      boost::filesystem::canonical("../../Tests/FsiDataFiles/0.csv").string() + "');");
  sql("CREATE VIEW test_view AS SELECT * FROM test_foreign_table;");
  sqlAndCompareResult({});
}

TEST_P(StorageDetailsSystemTableTest, DifferentPageSizes) {
  const auto page_size = GetParam();
  sql("CREATE TABLE test_table (c1 INTEGER) WITH (page_size = " +
      std::to_string(page_size) + ");");
  sql("INSERT INTO test_table VALUES (10);");

  auto db_id = getDbId("test_db");
  auto table_id = getTableId("test_table");
  StorageDetailsResult result;
  result.database_id = db_id;
  result.table_id = table_id;
  result.total_data_file_size = page_size * PAGES_PER_DATA_FILE;
  if (page_size == METADATA_PAGE_SIZE) {
    // In the case where the data page size is the same as the metadata page size, the
    // same (data) files will be used for both the data and metadata.
    result.metadata_file_count = 0;
    result.total_metadata_file_size = 0;
    result.total_metadata_page_count = 0;
    result.total_free_metadata_page_count = 0;
    // Both metadata and data pages for the defined integer column + the $deleted$ column
    result.total_free_data_page_count -= 4;
  } else {
    // One page for the defined integer column + the $deleted$ column
    result.total_free_metadata_page_count -= 2;
    result.total_free_data_page_count -= 2;
  }
  sqlAndCompareResult({result});
}

INSTANTIATE_TEST_SUITE_P(
    DifferentPageSizes,
    StorageDetailsSystemTableTest,
    testing::Values(100 /* Arbitrary page size */,
                    METADATA_PAGE_SIZE,
                    65536 /* Results in the same file size as the metadata file */),
    [](const auto& param_info) {
      return "Page_Size_" + std::to_string(param_info.param);
    });

int main(int argc, char** argv) {
  g_enable_fsi = true;
  g_enable_system_tables = true;
  TestHelpers::init_logger_stderr_only(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  DBHandlerTestFixture::initTestArgs(argc, argv);

  int err{0};
  try {
    err = RUN_ALL_TESTS();
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
  }
  g_enable_fsi = false;
  g_enable_system_tables = false;
  return err;
}
