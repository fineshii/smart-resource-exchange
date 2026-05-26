#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iostream>
#include <queue>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "sqlite3.h"

#pragma comment(lib, "ws2_32.lib")

struct Offer {
  int id;
  int resourceId;
  int userId;
  std::string studentName;
  int credits;
  std::string urgency;
  std::string mode;
  int bidValue;
  int priorityScore;
  int lockedCredits;
  std::string status;
  std::time_t timestamp;
};

struct Resource {
  int id;
  int ownerUserId;
  std::string title;
  std::string type;
  std::string owner;
  std::string description;
  std::string urgency;
  std::string mode;
  std::time_t deadline;
  std::vector<Offer> offers;
  std::string allocatedTo;
  int bestScore;
};

struct Internship {
  std::string company;
  std::string title;
  std::string deadline;
};

struct User {
  int id;
  std::string name;
  std::string email;
  int creditBalance;
  int lockedCredits;
  int availableCredits;
  std::string authToken;
};

std::vector<Resource> resources;
std::vector<Internship> internships;
std::vector<User> users;
sqlite3* database = nullptr;
CRITICAL_SECTION appLock;

const std::string DATA_DIR = "data";
const std::string DATABASE_FILE = "data/smart_resource_exchange.sqlite";

int lockedCreditsForUser(int userId);
int creditBalanceForUser(int userId);
void ensureSemesterGrant(int userId);

std::string escapeJson(const std::string& value) {
  std::ostringstream out;
  for (char ch : value) {
    if (ch == '"') out << "\\\"";
    else if (ch == '\\') out << "\\\\";
    else if (ch == '\n') out << "\\n";
    else out << ch;
  }
  return out.str();
}

int urgencyWeight(const std::string& urgency) {
  if (urgency == "High") return 30;
  if (urgency == "Medium") return 18;
  return 8;
}

int calculatePriorityScore(int credits, const std::string& urgency, int bidValue, const std::string& mode) {
  int bidComponent = mode == "Bidding" ? bidValue : 0;
  return (credits * 2) + urgencyWeight(urgency) + bidComponent;
}

bool isValidUrgency(const std::string& urgency) {
  return urgency == "High" || urgency == "Medium" || urgency == "Low";
}

bool isValidMode(const std::string& mode) {
  return mode == "Exchange" || mode == "Bidding";
}

std::string hashValue(const std::string& value) {
  unsigned long long hash = 1469598103934665603ULL;
  for (unsigned char ch : value) {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << hash;
  return out.str();
}

std::string passwordHash(const std::string& password) {
  return hashValue("smart-resource-exchange:" + password);
}

std::string createAuthToken(int userId, const std::string& email) {
  return hashValue(email + ":" + std::to_string(userId) + ":" + std::to_string(std::time(nullptr)));
}

std::string sqliteText(sqlite3_stmt* statement, int column) {
  const unsigned char* value = sqlite3_column_text(statement, column);
  return value ? reinterpret_cast<const char*>(value) : "";
}

bool executeSql(const std::string& sql) {
  char* error = nullptr;
  if (sqlite3_exec(database, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
    std::cerr << "SQLite error: " << (error ? error : "unknown error") << "\n";
    sqlite3_free(error);
    return false;
  }
  return true;
}

bool columnExists(const std::string& tableName, const std::string& columnName) {
  sqlite3_stmt* statement = nullptr;
  std::string sql = "PRAGMA table_info(" + tableName + ")";
  bool exists = false;
  if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK) {
    while (sqlite3_step(statement) == SQLITE_ROW) {
      if (sqliteText(statement, 1) == columnName) {
        exists = true;
        break;
      }
    }
  }
  sqlite3_finalize(statement);
  return exists;
}

bool addColumnIfMissing(const std::string& tableName, const std::string& columnName, const std::string& definition) {
  if (columnExists(tableName, columnName)) {
    return true;
  }
  return executeSql("ALTER TABLE " + tableName + " ADD COLUMN " + columnName + " " + definition + ";");
}

void bindText(sqlite3_stmt* statement, int index, const std::string& value) {
  sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

bool initDatabase() {
  CreateDirectoryA(DATA_DIR.c_str(), nullptr);
  if (sqlite3_open(DATABASE_FILE.c_str(), &database) != SQLITE_OK) {
    std::cerr << "Could not open SQLite database.\n";
    return false;
  }

  bool ok = executeSql(
    "PRAGMA foreign_keys = ON;"
    "CREATE TABLE IF NOT EXISTS resources ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  owner_user_id INTEGER DEFAULT 0,"
    "  title TEXT NOT NULL,"
    "  type TEXT NOT NULL,"
    "  owner TEXT NOT NULL,"
    "  description TEXT NOT NULL,"
    "  urgency TEXT NOT NULL,"
    "  mode TEXT NOT NULL,"
    "  deadline INTEGER NOT NULL,"
    "  allocated_to TEXT DEFAULT '',"
    "  best_score INTEGER DEFAULT 0"
    ");"
    "CREATE TABLE IF NOT EXISTS offers ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  resource_id INTEGER NOT NULL,"
    "  user_id INTEGER DEFAULT 0,"
    "  student_name TEXT NOT NULL,"
    "  credits INTEGER NOT NULL,"
    "  urgency TEXT NOT NULL,"
    "  mode TEXT NOT NULL,"
    "  bid_value INTEGER DEFAULT 0,"
    "  priority_score INTEGER NOT NULL,"
    "  locked_credits INTEGER DEFAULT 0,"
    "  status TEXT DEFAULT 'pending',"
    "  timestamp INTEGER NOT NULL,"
    "  FOREIGN KEY(resource_id) REFERENCES resources(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS users ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name TEXT NOT NULL UNIQUE,"
    "  email TEXT DEFAULT '',"
    "  password_hash TEXT DEFAULT '',"
    "  auth_token TEXT DEFAULT '',"
    "  credit_balance INTEGER NOT NULL DEFAULT 0,"
    "  created_at INTEGER NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS semesters ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name TEXT NOT NULL UNIQUE,"
    "  starts_on TEXT NOT NULL,"
    "  ends_on TEXT NOT NULL,"
    "  credit_grant INTEGER NOT NULL,"
    "  is_active INTEGER NOT NULL DEFAULT 0"
    ");"
    "CREATE TABLE IF NOT EXISTS credit_transactions ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  user_id INTEGER NOT NULL,"
    "  semester_id INTEGER,"
    "  resource_id INTEGER,"
    "  offer_id INTEGER,"
    "  amount INTEGER NOT NULL,"
    "  reason TEXT NOT NULL,"
    "  created_at INTEGER NOT NULL,"
    "  FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS internships ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  company TEXT NOT NULL,"
    "  title TEXT NOT NULL,"
    "  deadline TEXT NOT NULL"
    ");"
  );

  ok = ok && addColumnIfMissing("resources", "owner_user_id", "INTEGER DEFAULT 0");
  ok = ok && addColumnIfMissing("offers", "user_id", "INTEGER DEFAULT 0");
  ok = ok && addColumnIfMissing("offers", "locked_credits", "INTEGER DEFAULT 0");
  ok = ok && addColumnIfMissing("offers", "status", "TEXT DEFAULT 'pending'");
  ok = ok && addColumnIfMissing("users", "email", "TEXT DEFAULT ''");
  ok = ok && addColumnIfMissing("users", "password_hash", "TEXT DEFAULT ''");
  ok = ok && addColumnIfMissing("users", "auth_token", "TEXT DEFAULT ''");
  return ok;
}

int tableCount(const std::string& tableName) {
  std::string sql = "SELECT COUNT(*) FROM " + tableName;
  sqlite3_stmt* statement = nullptr;
  int count = 0;
  if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK &&
      sqlite3_step(statement) == SQLITE_ROW) {
    count = sqlite3_column_int(statement, 0);
  }
  sqlite3_finalize(statement);
  return count;
}

int intQuery(const std::string& sql, int fallback = 0) {
  sqlite3_stmt* statement = nullptr;
  int value = fallback;
  if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK &&
      sqlite3_step(statement) == SQLITE_ROW) {
    value = sqlite3_column_int(statement, 0);
  }
  sqlite3_finalize(statement);
  return value;
}

std::string activeSemesterName(std::time_t now) {
  std::tm* local = std::localtime(&now);
  int year = local ? local->tm_year + 1900 : 2026;
  int month = local ? local->tm_mon + 1 : 1;
  return month <= 6 ? "Spring " + std::to_string(year) : "Fall " + std::to_string(year);
}

void semesterDates(std::time_t now, std::string& startsOn, std::string& endsOn) {
  std::tm* local = std::localtime(&now);
  int year = local ? local->tm_year + 1900 : 2026;
  int month = local ? local->tm_mon + 1 : 1;
  if (month <= 6) {
    startsOn = std::to_string(year) + "-01-01";
    endsOn = std::to_string(year) + "-06-30";
  } else {
    startsOn = std::to_string(year) + "-07-01";
    endsOn = std::to_string(year) + "-12-31";
  }
}

int ensureActiveSemester() {
  std::time_t now = std::time(nullptr);
  std::string name = activeSemesterName(now);
  sqlite3_stmt* statement = nullptr;
  int semesterId = 0;

  sqlite3_prepare_v2(database, "SELECT id FROM semesters WHERE name = ?", -1, &statement, nullptr);
  bindText(statement, 1, name);
  if (sqlite3_step(statement) == SQLITE_ROW) {
    semesterId = sqlite3_column_int(statement, 0);
  }
  sqlite3_finalize(statement);

  if (semesterId == 0) {
    std::string startsOn;
    std::string endsOn;
    semesterDates(now, startsOn, endsOn);
    sqlite3_prepare_v2(database,
      "INSERT INTO semesters(name, starts_on, ends_on, credit_grant, is_active) VALUES (?, ?, ?, 100, 1)",
      -1, &statement, nullptr);
    bindText(statement, 1, name);
    bindText(statement, 2, startsOn);
    bindText(statement, 3, endsOn);
    sqlite3_step(statement);
    sqlite3_finalize(statement);
    semesterId = static_cast<int>(sqlite3_last_insert_rowid(database));
  }

  executeSql("UPDATE semesters SET is_active = 0 WHERE id != " + std::to_string(semesterId));
  executeSql("UPDATE semesters SET is_active = 1 WHERE id = " + std::to_string(semesterId));
  return semesterId;
}

void recordCreditTransaction(int userId, int semesterId, int resourceId, int offerId,
                             int amount, const std::string& reason) {
  sqlite3_stmt* statement = nullptr;
  sqlite3_prepare_v2(database,
    "INSERT INTO credit_transactions(user_id, semester_id, resource_id, offer_id, amount, reason, created_at) "
    "VALUES (?, ?, ?, ?, ?, ?, ?)",
    -1, &statement, nullptr);
  sqlite3_bind_int(statement, 1, userId);
  if (semesterId > 0) sqlite3_bind_int(statement, 2, semesterId);
  else sqlite3_bind_null(statement, 2);
  if (resourceId > 0) sqlite3_bind_int(statement, 3, resourceId);
  else sqlite3_bind_null(statement, 3);
  if (offerId > 0) sqlite3_bind_int(statement, 4, offerId);
  else sqlite3_bind_null(statement, 4);
  sqlite3_bind_int(statement, 5, amount);
  bindText(statement, 6, reason);
  sqlite3_bind_int64(statement, 7, static_cast<sqlite3_int64>(std::time(nullptr)));
  sqlite3_step(statement);
  sqlite3_finalize(statement);
}

void adjustUserCredits(int userId, int amount) {
  sqlite3_stmt* statement = nullptr;
  sqlite3_prepare_v2(database,
    "UPDATE users SET credit_balance = credit_balance + ? WHERE id = ?",
    -1, &statement, nullptr);
  sqlite3_bind_int(statement, 1, amount);
  sqlite3_bind_int(statement, 2, userId);
  sqlite3_step(statement);
  sqlite3_finalize(statement);
}

int findUserId(const std::string& name) {
  sqlite3_stmt* statement = nullptr;
  int userId = 0;
  sqlite3_prepare_v2(database, "SELECT id FROM users WHERE name = ?", -1, &statement, nullptr);
  bindText(statement, 1, name);
  if (sqlite3_step(statement) == SQLITE_ROW) {
    userId = sqlite3_column_int(statement, 0);
  }
  sqlite3_finalize(statement);
  return userId;
}

User userByStatement(sqlite3_stmt* statement) {
  User user;
  user.id = sqlite3_column_int(statement, 0);
  user.name = sqliteText(statement, 1);
  user.email = sqliteText(statement, 2);
  user.creditBalance = sqlite3_column_int(statement, 3);
  user.authToken = sqliteText(statement, 4);
  user.lockedCredits = lockedCreditsForUser(user.id);
  user.availableCredits = user.creditBalance - user.lockedCredits;
  return user;
}

bool loadUserByToken(const std::string& authToken, User& user) {
  if (authToken.empty()) return false;
  sqlite3_stmt* statement = nullptr;
  bool found = false;
  sqlite3_prepare_v2(database,
    "SELECT id, name, email, credit_balance, auth_token FROM users WHERE auth_token = ?",
    -1, &statement, nullptr);
  bindText(statement, 1, authToken);
  if (sqlite3_step(statement) == SQLITE_ROW) {
    user = userByStatement(statement);
    ensureSemesterGrant(user.id);
    user.creditBalance = creditBalanceForUser(user.id);
    user.lockedCredits = lockedCreditsForUser(user.id);
    user.availableCredits = user.creditBalance - user.lockedCredits;
    found = true;
  }
  sqlite3_finalize(statement);
  return found;
}

bool emailExists(const std::string& email) {
  sqlite3_stmt* statement = nullptr;
  bool exists = false;
  sqlite3_prepare_v2(database, "SELECT id FROM users WHERE email = ?", -1, &statement, nullptr);
  bindText(statement, 1, email);
  exists = sqlite3_step(statement) == SQLITE_ROW;
  sqlite3_finalize(statement);
  return exists;
}

bool nameExists(const std::string& name) {
  return findUserId(name) != 0;
}

void ensureSemesterGrant(int userId) {
  int semesterId = ensureActiveSemester();
  sqlite3_stmt* statement = nullptr;
  int alreadyGranted = 0;
  sqlite3_prepare_v2(database,
    "SELECT COUNT(*) FROM credit_transactions WHERE user_id = ? AND semester_id = ? AND reason = 'semester_grant'",
    -1, &statement, nullptr);
  sqlite3_bind_int(statement, 1, userId);
  sqlite3_bind_int(statement, 2, semesterId);
  if (sqlite3_step(statement) == SQLITE_ROW) {
    alreadyGranted = sqlite3_column_int(statement, 0);
  }
  sqlite3_finalize(statement);

  if (alreadyGranted == 0) {
    int grant = intQuery("SELECT credit_grant FROM semesters WHERE id = " + std::to_string(semesterId), 100);
    adjustUserCredits(userId, grant);
    recordCreditTransaction(userId, semesterId, 0, 0, grant, "semester_grant");
  }
}

int ensureUser(const std::string& name) {
  int userId = findUserId(name);
  if (userId == 0) {
    sqlite3_stmt* statement = nullptr;
    sqlite3_prepare_v2(database,
      "INSERT INTO users(name, credit_balance, created_at) VALUES (?, 0, ?)",
      -1, &statement, nullptr);
    bindText(statement, 1, name);
    sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(std::time(nullptr)));
    sqlite3_step(statement);
    sqlite3_finalize(statement);
    userId = static_cast<int>(sqlite3_last_insert_rowid(database));
  }
  ensureSemesterGrant(userId);
  return userId;
}

std::string userJson(const User& user) {
  std::ostringstream json;
  json << "{"
       << "\"id\":" << user.id << ","
       << "\"name\":\"" << escapeJson(user.name) << "\","
       << "\"email\":\"" << escapeJson(user.email) << "\","
       << "\"creditBalance\":" << user.creditBalance << ","
       << "\"lockedCredits\":" << user.lockedCredits << ","
       << "\"availableCredits\":" << user.availableCredits << ","
       << "\"authToken\":\"" << escapeJson(user.authToken) << "\""
       << "}";
  return json.str();
}

int lockedCreditsForUser(int userId) {
  sqlite3_stmt* statement = nullptr;
  int locked = 0;
  sqlite3_prepare_v2(database,
    "SELECT COALESCE(SUM(locked_credits), 0) FROM offers WHERE user_id = ? AND status = 'pending'",
    -1, &statement, nullptr);
  sqlite3_bind_int(statement, 1, userId);
  if (sqlite3_step(statement) == SQLITE_ROW) {
    locked = sqlite3_column_int(statement, 0);
  }
  sqlite3_finalize(statement);
  return locked;
}

int creditBalanceForUser(int userId) {
  sqlite3_stmt* statement = nullptr;
  int balance = 0;
  sqlite3_prepare_v2(database, "SELECT credit_balance FROM users WHERE id = ?", -1, &statement, nullptr);
  sqlite3_bind_int(statement, 1, userId);
  if (sqlite3_step(statement) == SQLITE_ROW) {
    balance = sqlite3_column_int(statement, 0);
  }
  sqlite3_finalize(statement);
  return balance;
}

int availableCreditsForUser(int userId) {
  return creditBalanceForUser(userId) - lockedCreditsForUser(userId);
}

void insertResource(int ownerUserId, const std::string& title, const std::string& type, const std::string& owner,
                    const std::string& description, const std::string& urgency,
                    const std::string& mode, std::time_t deadline) {
  sqlite3_stmt* statement = nullptr;
  sqlite3_prepare_v2(database,
    "INSERT INTO resources(owner_user_id, title, type, owner, description, urgency, mode, deadline, allocated_to, best_score) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, '', 0)",
    -1, &statement, nullptr);
  sqlite3_bind_int(statement, 1, ownerUserId);
  bindText(statement, 2, title);
  bindText(statement, 3, type);
  bindText(statement, 4, owner);
  bindText(statement, 5, description);
  bindText(statement, 6, urgency);
  bindText(statement, 7, mode);
  sqlite3_bind_int64(statement, 8, static_cast<sqlite3_int64>(deadline));
  sqlite3_step(statement);
  sqlite3_finalize(statement);
}

void insertOffer(int resourceId, int userId, const std::string& studentName, int credits, const std::string& urgency,
                 const std::string& mode, int bidValue, int priorityScore, int lockedCredits, std::time_t timestamp) {
  sqlite3_stmt* statement = nullptr;
  sqlite3_prepare_v2(database,
    "INSERT INTO offers(resource_id, user_id, student_name, credits, urgency, mode, bid_value, priority_score, locked_credits, status, timestamp) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 'pending', ?)",
    -1, &statement, nullptr);
  sqlite3_bind_int(statement, 1, resourceId);
  sqlite3_bind_int(statement, 2, userId);
  bindText(statement, 3, studentName);
  sqlite3_bind_int(statement, 4, credits);
  bindText(statement, 5, urgency);
  bindText(statement, 6, mode);
  sqlite3_bind_int(statement, 7, bidValue);
  sqlite3_bind_int(statement, 8, priorityScore);
  sqlite3_bind_int(statement, 9, lockedCredits);
  sqlite3_bind_int64(statement, 10, static_cast<sqlite3_int64>(timestamp));
  sqlite3_step(statement);
  sqlite3_finalize(statement);
}

void seedData() {
  std::time_t now = std::time(nullptr);
  int aaravId = ensureUser("Aarav");
  int meeraId = ensureUser("Meera");
  int rohanId = ensureUser("Rohan");
  int nishaId = ensureUser("Nisha");
  int kabirId = ensureUser("Kabir");
  int ishaId = ensureUser("Isha");

  insertResource(aaravId, "DAA Reference Book", "Book", "Aarav",
    "Algorithms textbook available for two weeks. Best for exam preparation and project viva practice.",
    "High", "Exchange", now + (3 * 24 * 60 * 60));
  insertResource(meeraId, "Scientific Calculator", "Calculator", "Meera",
    "Casio scientific calculator available for the next lab cycle. Clean condition with working battery.",
    "Medium", "Bidding", now + (5 * 24 * 60 * 60));
  insertResource(rohanId, "Physics Lab Manual Notes", "Notes", "Rohan",
    "Clean handwritten readings and experiment observations for revision before practicals.",
    "Low", "Exchange", now + (7 * 24 * 60 * 60));

  insertOffer(1, nishaId, "Nisha", 50, "High", "Exchange", 0, calculatePriorityScore(50, "High", 0, "Exchange"), 0, now - 40);
  insertOffer(1, kabirId, "Kabir", 50, "Medium", "Exchange", 0, calculatePriorityScore(50, "Medium", 0, "Exchange"), 0, now - 20);
  insertOffer(2, ishaId, "Isha", 50, "Medium", "Bidding", 45, calculatePriorityScore(50, "Medium", 45, "Bidding"), 45, now - 15);

  executeSql(
    "INSERT INTO internships(company, title, deadline) VALUES "
    "('Tata Consultancy Services', 'Campus internship applications', '2026-06-05'),"
    "('Infosys Springboard', 'Scholarship and certification track', '2026-06-12'),"
    "('AICTE', 'Virtual internship program', '2026-06-20')"
  );
}

void ensureSeedData() {
  if (tableCount("resources") == 0) {
    seedData();
  }
}

void migrateLegacyUsers() {
  sqlite3_stmt* statement = nullptr;
  sqlite3_prepare_v2(database, "SELECT id, owner FROM resources WHERE owner_user_id = 0", -1, &statement, nullptr);
  std::vector<std::pair<int, std::string>> resourceOwners;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    resourceOwners.push_back({sqlite3_column_int(statement, 0), sqliteText(statement, 1)});
  }
  sqlite3_finalize(statement);

  for (const auto& item : resourceOwners) {
    int userId = ensureUser(item.second);
    sqlite3_stmt* update = nullptr;
    sqlite3_prepare_v2(database, "UPDATE resources SET owner_user_id = ? WHERE id = ?", -1, &update, nullptr);
    sqlite3_bind_int(update, 1, userId);
    sqlite3_bind_int(update, 2, item.first);
    sqlite3_step(update);
    sqlite3_finalize(update);
  }

  sqlite3_prepare_v2(database, "SELECT id, student_name, mode, bid_value, status FROM offers WHERE user_id = 0", -1, &statement, nullptr);
  struct LegacyOfferUpdate {
    int offerId;
    int userId;
    int lockedCredits;
    std::string status;
  };
  std::vector<LegacyOfferUpdate> offerUpdates;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    int offerId = sqlite3_column_int(statement, 0);
    std::string studentName = sqliteText(statement, 1);
    std::string mode = sqliteText(statement, 2);
    int bidValue = sqlite3_column_int(statement, 3);
    std::string status = sqliteText(statement, 4);
    if (status.empty()) status = "pending";
    int lockedCredits = mode == "Bidding" && status == "pending" ? bidValue : 0;
    offerUpdates.push_back({offerId, ensureUser(studentName), lockedCredits, status});
  }
  sqlite3_finalize(statement);

  for (const LegacyOfferUpdate& item : offerUpdates) {
    sqlite3_stmt* update = nullptr;
    sqlite3_prepare_v2(database,
      "UPDATE offers SET user_id = ?, locked_credits = ?, status = ? WHERE id = ?",
      -1, &update, nullptr);
    sqlite3_bind_int(update, 1, item.userId);
    sqlite3_bind_int(update, 2, item.lockedCredits);
    bindText(update, 3, item.status);
    sqlite3_bind_int(update, 4, item.offerId);
    sqlite3_step(update);
    sqlite3_finalize(update);
  }
}

void loadData() {
  resources.clear();
  internships.clear();
  users.clear();

  sqlite3_stmt* statement = nullptr;
  sqlite3_prepare_v2(database,
    "SELECT id, owner_user_id, title, type, owner, description, urgency, mode, deadline, allocated_to, best_score "
    "FROM resources ORDER BY allocated_to = '', deadline ASC, id ASC",
    -1, &statement, nullptr);

  while (sqlite3_step(statement) == SQLITE_ROW) {
    Resource resource;
    resource.id = sqlite3_column_int(statement, 0);
    resource.ownerUserId = sqlite3_column_int(statement, 1);
    resource.title = sqliteText(statement, 2);
    resource.type = sqliteText(statement, 3);
    resource.owner = sqliteText(statement, 4);
    resource.description = sqliteText(statement, 5);
    resource.urgency = sqliteText(statement, 6);
    resource.mode = sqliteText(statement, 7);
    resource.deadline = static_cast<std::time_t>(sqlite3_column_int64(statement, 8));
    resource.allocatedTo = sqliteText(statement, 9);
    resource.bestScore = sqlite3_column_int(statement, 10);
    resources.push_back(resource);
  }
  sqlite3_finalize(statement);

  sqlite3_prepare_v2(database,
    "SELECT id, resource_id, user_id, student_name, credits, urgency, mode, bid_value, priority_score, locked_credits, status, timestamp "
    "FROM offers ORDER BY timestamp ASC",
    -1, &statement, nullptr);

  while (sqlite3_step(statement) == SQLITE_ROW) {
    Offer offer;
    offer.id = sqlite3_column_int(statement, 0);
    offer.resourceId = sqlite3_column_int(statement, 1);
    offer.userId = sqlite3_column_int(statement, 2);
    offer.studentName = sqliteText(statement, 3);
    offer.credits = sqlite3_column_int(statement, 4);
    offer.urgency = sqliteText(statement, 5);
    offer.mode = sqliteText(statement, 6);
    offer.bidValue = sqlite3_column_int(statement, 7);
    offer.priorityScore = sqlite3_column_int(statement, 8);
    offer.lockedCredits = sqlite3_column_int(statement, 9);
    offer.status = sqliteText(statement, 10);
    offer.timestamp = static_cast<std::time_t>(sqlite3_column_int64(statement, 11));

    auto it = std::find_if(resources.begin(), resources.end(), [&offer](const Resource& resource) {
      return resource.id == offer.resourceId;
    });
    if (it != resources.end()) {
      it->offers.push_back(offer);
    }
  }
  sqlite3_finalize(statement);

  sqlite3_prepare_v2(database,
    "SELECT company, title, deadline FROM internships ORDER BY deadline ASC",
    -1, &statement, nullptr);
  while (sqlite3_step(statement) == SQLITE_ROW) {
    internships.push_back({sqliteText(statement, 0), sqliteText(statement, 1), sqliteText(statement, 2)});
  }
  sqlite3_finalize(statement);

  sqlite3_prepare_v2(database,
    "SELECT id, name, email, credit_balance, auth_token FROM users ORDER BY name ASC",
    -1, &statement, nullptr);
  while (sqlite3_step(statement) == SQLITE_ROW) {
    users.push_back(userByStatement(statement));
  }
  sqlite3_finalize(statement);
}

void updateAllocation(int resourceId, const std::string& allocatedTo, int bestScore) {
  sqlite3_stmt* statement = nullptr;
  sqlite3_prepare_v2(database,
    "UPDATE resources SET allocated_to = ?, best_score = ? WHERE id = ?",
    -1, &statement, nullptr);
  bindText(statement, 1, allocatedTo);
  sqlite3_bind_int(statement, 2, bestScore);
  sqlite3_bind_int(statement, 3, resourceId);
  sqlite3_step(statement);
  sqlite3_finalize(statement);
}

void updateOfferStatus(int offerId, const std::string& status, int lockedCredits) {
  sqlite3_stmt* statement = nullptr;
  sqlite3_prepare_v2(database,
    "UPDATE offers SET status = ?, locked_credits = ? WHERE id = ?",
    -1, &statement, nullptr);
  bindText(statement, 1, status);
  sqlite3_bind_int(statement, 2, lockedCredits);
  sqlite3_bind_int(statement, 3, offerId);
  sqlite3_step(statement);
  sqlite3_finalize(statement);
}

bool allocateExpiredResources() {
  std::time_t now = std::time(nullptr);
  bool changed = false;

  for (Resource& resource : resources) {
    if (!resource.allocatedTo.empty() || now < resource.deadline || resource.offers.empty()) {
      continue;
    }

    auto compare = [](const Offer& left, const Offer& right) {
      if (left.priorityScore == right.priorityScore) {
        return left.timestamp > right.timestamp;
      }
      return left.priorityScore < right.priorityScore;
    };

    std::priority_queue<Offer, std::vector<Offer>, decltype(compare)> heap(compare);
    for (const Offer& offer : resource.offers) {
      if (offer.status == "pending") {
        heap.push(offer);
      }
    }

    if (heap.empty()) {
      continue;
    }

    Offer winner = heap.top();
    resource.allocatedTo = winner.studentName;
    resource.bestScore = winner.priorityScore;
    updateAllocation(resource.id, winner.studentName, winner.priorityScore);

    for (const Offer& offer : resource.offers) {
      if (offer.status != "pending") {
        continue;
      }
      if (offer.id == winner.id) {
        updateOfferStatus(offer.id, "won", 0);
        if (offer.mode == "Bidding" && offer.lockedCredits > 0 && offer.userId > 0 && resource.ownerUserId > 0) {
          adjustUserCredits(offer.userId, -offer.lockedCredits);
          adjustUserCredits(resource.ownerUserId, offer.lockedCredits);
          recordCreditTransaction(offer.userId, ensureActiveSemester(), resource.id, offer.id,
                                  -offer.lockedCredits, "winning_bid_paid");
          recordCreditTransaction(resource.ownerUserId, ensureActiveSemester(), resource.id, offer.id,
                                  offer.lockedCredits, "sale_received");
        }
      } else {
        updateOfferStatus(offer.id, "lost", 0);
      }
    }
    changed = true;
  }

  return changed;
}

std::string resourcesJson() {
  loadData();
  if (allocateExpiredResources()) {
    loadData();
  }

  std::ostringstream json;
  json << "{\"resources\":[";
  for (size_t i = 0; i < resources.size(); ++i) {
    const Resource& r = resources[i];
    if (i) json << ",";
    json << "{"
         << "\"id\":" << r.id << ","
         << "\"ownerUserId\":" << r.ownerUserId << ","
         << "\"title\":\"" << escapeJson(r.title) << "\","
         << "\"type\":\"" << escapeJson(r.type) << "\","
         << "\"owner\":\"" << escapeJson(r.owner) << "\","
         << "\"description\":\"" << escapeJson(r.description) << "\","
         << "\"urgency\":\"" << escapeJson(r.urgency) << "\","
         << "\"mode\":\"" << escapeJson(r.mode) << "\","
         << "\"deadline\":" << static_cast<long long>(r.deadline) << ","
         << "\"offerCount\":" << r.offers.size() << ","
         << "\"allocatedTo\":\"" << escapeJson(r.allocatedTo) << "\","
         << "\"bestScore\":" << r.bestScore
         << "}";
  }

  json << "],\"users\":[";
  for (size_t i = 0; i < users.size(); ++i) {
    const User& user = users[i];
    if (i) json << ",";
    json << "{"
         << "\"id\":" << user.id << ","
         << "\"name\":\"" << escapeJson(user.name) << "\","
         << "\"creditBalance\":" << user.creditBalance << ","
         << "\"lockedCredits\":" << user.lockedCredits << ","
         << "\"availableCredits\":" << user.availableCredits
         << "}";
  }

  int semesterId = ensureActiveSemester();
  std::string semesterName;
  int semesterGrant = 100;
  sqlite3_stmt* semesterStatement = nullptr;
  sqlite3_prepare_v2(database, "SELECT name, credit_grant FROM semesters WHERE id = ?", -1, &semesterStatement, nullptr);
  sqlite3_bind_int(semesterStatement, 1, semesterId);
  if (sqlite3_step(semesterStatement) == SQLITE_ROW) {
    semesterName = sqliteText(semesterStatement, 0);
    semesterGrant = sqlite3_column_int(semesterStatement, 1);
  }
  sqlite3_finalize(semesterStatement);

  json << "],\"semester\":{"
       << "\"id\":" << semesterId << ","
       << "\"name\":\"" << escapeJson(semesterName) << "\","
       << "\"creditGrant\":" << semesterGrant
       << "},\"internships\":[";
  for (size_t i = 0; i < internships.size(); ++i) {
    const Internship& item = internships[i];
    if (i) json << ",";
    json << "{"
         << "\"company\":\"" << escapeJson(item.company) << "\","
         << "\"title\":\"" << escapeJson(item.title) << "\","
         << "\"deadline\":\"" << escapeJson(item.deadline) << "\""
         << "}";
  }
  json << "]}";
  return json.str();
}

std::string parseStringField(const std::string& body, const std::string& key) {
  std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch match;
  return std::regex_search(body, match, pattern) ? match[1].str() : "";
}

int parseIntField(const std::string& body, const std::string& key, int fallback = 0) {
  std::regex pattern("\"" + key + "\"\\s*:\\s*(-?[0-9]+)");
  std::smatch match;
  return std::regex_search(body, match, pattern) ? std::stoi(match[1].str()) : fallback;
}

std::string submitOffer(const std::string& body, int& statusCode) {
  loadData();
  allocateExpiredResources();

  int resourceId = parseIntField(body, "resourceId");
  std::string authToken = parseStringField(body, "authToken");
  std::string urgency = parseStringField(body, "urgency");
  std::string mode = parseStringField(body, "mode");
  int bidValue = parseIntField(body, "bidValue");

  User currentUser;
  if (!loadUserByToken(authToken, currentUser)) {
    statusCode = 401;
    return "{\"error\":\"Please log in before making an offer.\"}";
  }

  if (resourceId <= 0) {
    statusCode = 400;
    return "{\"error\":\"Resource is required.\"}";
  }

  if (!isValidUrgency(urgency)) {
    statusCode = 400;
    return "{\"error\":\"Urgency must be High, Medium, or Low.\"}";
  }

  if (!isValidMode(mode)) {
    statusCode = 400;
    return "{\"error\":\"Offer mode must be Exchange or Bidding.\"}";
  }

  if (bidValue < 0 || bidValue > 10000) {
    statusCode = 400;
    return "{\"error\":\"Bid value must be between 0 and 10000.\"}";
  }

  auto it = std::find_if(resources.begin(), resources.end(), [resourceId](const Resource& resource) {
    return resource.id == resourceId;
  });

  if (it == resources.end()) {
    statusCode = 404;
    return "{\"error\":\"Resource not found.\"}";
  }

  int userId = currentUser.id;
  int availableCredits = availableCreditsForUser(userId);

  if (it->ownerUserId == userId) {
    statusCode = 400;
    return "{\"error\":\"You cannot make an offer on your own resource.\"}";
  }

  if (!it->allocatedTo.empty() || std::time(nullptr) >= it->deadline) {
    statusCode = 409;
    return "{\"error\":\"Deadline has passed. This resource is already closed.\"}";
  }

  if (mode != it->mode) {
    statusCode = 400;
    return "{\"error\":\"Offer mode must match the resource exchange mode.\"}";
  }

  if (mode == "Exchange" && bidValue != 0) {
    statusCode = 400;
    return "{\"error\":\"Exchange offers cannot include a bid value.\"}";
  }

  if (mode == "Bidding" && bidValue == 0) {
    statusCode = 400;
    return "{\"error\":\"Bidding offers must include a bid value greater than zero.\"}";
  }

  if (mode == "Bidding" && bidValue > availableCredits) {
    statusCode = 400;
    return "{\"error\":\"Bid exceeds the student's available credits.\"}";
  }

  for (const Offer& offer : it->offers) {
    if (offer.userId == userId && offer.status == "pending") {
      statusCode = 409;
      return "{\"error\":\"This student already has a pending offer for this resource.\"}";
    }
  }

  int creditScore = std::min(availableCredits, 50);
  int lockedCredits = mode == "Bidding" ? bidValue : 0;
  int score = calculatePriorityScore(creditScore, urgency, bidValue, mode);
  insertOffer(resourceId, userId, currentUser.name, creditScore, urgency, mode, bidValue, score, lockedCredits, std::time(nullptr));

  statusCode = 201;
  std::ostringstream response;
  response << "{\"ok\":true,\"score\":" << score
           << ",\"availableCredits\":" << availableCredits - lockedCredits << "}";
  return response.str();
}

std::string createResource(const std::string& body, int& statusCode) {
  std::string title = parseStringField(body, "title");
  std::string type = parseStringField(body, "type");
  std::string authToken = parseStringField(body, "authToken");
  std::string description = parseStringField(body, "description");
  std::string urgency = parseStringField(body, "urgency");
  std::string mode = parseStringField(body, "mode");
  int durationMinutes = parseIntField(body, "durationMinutes", 180);

  User currentUser;
  if (!loadUserByToken(authToken, currentUser)) {
    statusCode = 401;
    return "{\"error\":\"Please log in before listing a resource.\"}";
  }

  if (title.empty() || type.empty() || description.empty()) {
    statusCode = 400;
    return "{\"error\":\"Title, type, and description are required.\"}";
  }

  if (!isValidUrgency(urgency)) {
    urgency = "Medium";
  }
  if (!isValidMode(mode)) {
    mode = "Exchange";
  }
  if (durationMinutes < 5) {
    durationMinutes = 5;
  }

  std::time_t deadline = std::time(nullptr) + (durationMinutes * 60);
  insertResource(currentUser.id, title, type, currentUser.name, description, urgency, mode, deadline);

  statusCode = 201;
  std::ostringstream response;
  response << "{\"ok\":true,\"id\":" << sqlite3_last_insert_rowid(database) << "}";
  return response.str();
}

std::string registerUser(const std::string& body, int& statusCode) {
  std::string name = parseStringField(body, "name");
  std::string email = parseStringField(body, "email");
  std::string password = parseStringField(body, "password");

  if (name.empty() || email.empty() || password.empty()) {
    statusCode = 400;
    return "{\"error\":\"Name, email, and password are required.\"}";
  }

  if (password.size() < 4) {
    statusCode = 400;
    return "{\"error\":\"Password must be at least 4 characters.\"}";
  }

  if (emailExists(email)) {
    statusCode = 409;
    return "{\"error\":\"An account with this email already exists.\"}";
  }

  if (nameExists(name)) {
    statusCode = 409;
    return "{\"error\":\"This display name is already taken.\"}";
  }

  sqlite3_stmt* statement = nullptr;
  std::string token = createAuthToken(0, email);
  sqlite3_prepare_v2(database,
    "INSERT INTO users(name, email, password_hash, auth_token, credit_balance, created_at) VALUES (?, ?, ?, ?, 0, ?)",
    -1, &statement, nullptr);
  bindText(statement, 1, name);
  bindText(statement, 2, email);
  bindText(statement, 3, passwordHash(password));
  bindText(statement, 4, token);
  sqlite3_bind_int64(statement, 5, static_cast<sqlite3_int64>(std::time(nullptr)));
  if (sqlite3_step(statement) != SQLITE_DONE) {
    sqlite3_finalize(statement);
    statusCode = 500;
    return "{\"error\":\"Account could not be created.\"}";
  }
  sqlite3_finalize(statement);

  int userId = static_cast<int>(sqlite3_last_insert_rowid(database));
  token = createAuthToken(userId, email);
  sqlite3_prepare_v2(database, "UPDATE users SET auth_token = ? WHERE id = ?", -1, &statement, nullptr);
  bindText(statement, 1, token);
  sqlite3_bind_int(statement, 2, userId);
  sqlite3_step(statement);
  sqlite3_finalize(statement);

  ensureSemesterGrant(userId);
  User user;
  loadUserByToken(token, user);
  statusCode = 201;
  return "{\"ok\":true,\"user\":" + userJson(user) + "}";
}

std::string loginUser(const std::string& body, int& statusCode) {
  std::string email = parseStringField(body, "email");
  std::string password = parseStringField(body, "password");

  if (email.empty() || password.empty()) {
    statusCode = 400;
    return "{\"error\":\"Email and password are required.\"}";
  }

  sqlite3_stmt* statement = nullptr;
  int userId = 0;
  std::string storedHash;
  std::string name;
  sqlite3_prepare_v2(database,
    "SELECT id, name, password_hash FROM users WHERE email = ?",
    -1, &statement, nullptr);
  bindText(statement, 1, email);
  if (sqlite3_step(statement) == SQLITE_ROW) {
    userId = sqlite3_column_int(statement, 0);
    name = sqliteText(statement, 1);
    storedHash = sqliteText(statement, 2);
  }
  sqlite3_finalize(statement);

  if (userId == 0 || storedHash.empty() || storedHash != passwordHash(password)) {
    statusCode = 401;
    return "{\"error\":\"Invalid email or password.\"}";
  }

  std::string token = createAuthToken(userId, email);
  sqlite3_prepare_v2(database, "UPDATE users SET auth_token = ? WHERE id = ?", -1, &statement, nullptr);
  bindText(statement, 1, token);
  sqlite3_bind_int(statement, 2, userId);
  sqlite3_step(statement);
  sqlite3_finalize(statement);

  ensureSemesterGrant(userId);
  User user;
  loadUserByToken(token, user);
  statusCode = 200;
  return "{\"ok\":true,\"user\":" + userJson(user) + "}";
}

std::string sessionUser(const std::string& body, int& statusCode) {
  User user;
  if (!loadUserByToken(parseStringField(body, "authToken"), user)) {
    statusCode = 401;
    return "{\"error\":\"Session expired. Please log in again.\"}";
  }
  statusCode = 200;
  return "{\"ok\":true,\"user\":" + userJson(user) + "}";
}

std::string contentTypeFor(const std::string& path) {
  if (path.find(".css") != std::string::npos) return "text/css; charset=utf-8";
  if (path.find(".js") != std::string::npos) return "application/javascript; charset=utf-8";
  if (path.find(".json") != std::string::npos) return "application/json; charset=utf-8";
  if (path.find(".webmanifest") != std::string::npos) return "application/manifest+json; charset=utf-8";
  return "text/html; charset=utf-8";
}

bool readFile(const std::string& requestPath, std::string& data, std::string& contentType) {
  std::string cleanPath = requestPath;
  size_t queryPosition = cleanPath.find('?');
  if (queryPosition != std::string::npos) {
    cleanPath = cleanPath.substr(0, queryPosition);
  }

  std::string safePath = cleanPath == "/" ? "/index.html" : cleanPath;
  if (safePath.find("..") != std::string::npos) {
    return false;
  }

  std::string filePath = "public" + safePath;
  std::ifstream file(filePath.c_str(), std::ios::binary);
  if (!file) {
    return false;
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  data = buffer.str();
  contentType = contentTypeFor(filePath);
  return true;
}

std::string httpResponse(int statusCode, const std::string& body, const std::string& contentType) {
  std::string reason = statusCode == 200 ? "OK" :
                       statusCode == 201 ? "Created" :
                       statusCode == 400 ? "Bad Request" :
                       statusCode == 401 ? "Unauthorized" :
                       statusCode == 404 ? "Not Found" :
                       statusCode == 409 ? "Conflict" : "Server Error";
  std::ostringstream response;
  response << "HTTP/1.1 " << statusCode << " " << reason << "\r\n"
           << "Content-Type: " << contentType << "\r\n"
           << "Cache-Control: no-store\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << body;
  return response.str();
}

void handleClient(SOCKET client) {
  int timeoutMs = 3000;
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

  std::string request;
  char buffer[4096];
  int received = 0;

  while ((received = recv(client, buffer, sizeof(buffer), 0)) > 0) {
    request.append(buffer, received);
    if (request.find("\r\n\r\n") != std::string::npos) {
      std::smatch match;
      std::regex lengthPattern("Content-Length:\\s*([0-9]+)", std::regex_constants::icase);
      int contentLength = std::regex_search(request, match, lengthPattern) ? std::stoi(match[1].str()) : 0;
      size_t bodyStart = request.find("\r\n\r\n") + 4;
      if (request.size() >= bodyStart + static_cast<size_t>(contentLength)) {
        break;
      }
    }
  }

  std::istringstream requestStream(request);
  std::string method;
  std::string path;
  requestStream >> method >> path;

  std::string body;
  size_t bodyStart = request.find("\r\n\r\n");
  if (bodyStart != std::string::npos) {
    body = request.substr(bodyStart + 4);
  }

  int statusCode = 200;
  std::string responseBody;
  std::string contentType = "application/json; charset=utf-8";

  if (method == "GET" && path == "/api/resources") {
    EnterCriticalSection(&appLock);
    responseBody = resourcesJson();
    LeaveCriticalSection(&appLock);
  } else if (method == "POST" && path == "/api/register") {
    EnterCriticalSection(&appLock);
    responseBody = registerUser(body, statusCode);
    LeaveCriticalSection(&appLock);
  } else if (method == "POST" && path == "/api/login") {
    EnterCriticalSection(&appLock);
    responseBody = loginUser(body, statusCode);
    LeaveCriticalSection(&appLock);
  } else if (method == "POST" && path == "/api/session") {
    EnterCriticalSection(&appLock);
    responseBody = sessionUser(body, statusCode);
    LeaveCriticalSection(&appLock);
  } else if (method == "POST" && path == "/api/offers") {
    EnterCriticalSection(&appLock);
    responseBody = submitOffer(body, statusCode);
    LeaveCriticalSection(&appLock);
  } else if (method == "POST" && path == "/api/resources") {
    EnterCriticalSection(&appLock);
    responseBody = createResource(body, statusCode);
    LeaveCriticalSection(&appLock);
  } else if (method == "GET") {
    if (!readFile(path, responseBody, contentType)) {
      statusCode = 404;
      responseBody = "Not found";
      contentType = "text/plain; charset=utf-8";
    }
  } else {
    statusCode = 400;
    responseBody = "{\"error\":\"Unsupported request.\"}";
  }

  std::string response = httpResponse(statusCode, responseBody, contentType);
  send(client, response.c_str(), static_cast<int>(response.size()), 0);
  closesocket(client);
}

DWORD WINAPI clientThread(LPVOID parameter) {
  SOCKET client = static_cast<SOCKET>(reinterpret_cast<UINT_PTR>(parameter));
  handleClient(client);
  return 0;
}

int main() {
  InitializeCriticalSection(&appLock);
  if (!initDatabase()) {
    DeleteCriticalSection(&appLock);
    return 1;
  }
  ensureSeedData();
  migrateLegacyUsers();

  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    std::cerr << "Winsock startup failed.\n";
    sqlite3_close(database);
    DeleteCriticalSection(&appLock);
    return 1;
  }

  SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (serverSocket == INVALID_SOCKET) {
    std::cerr << "Could not create server socket.\n";
    WSACleanup();
    sqlite3_close(database);
    DeleteCriticalSection(&appLock);
    return 1;
  }

  sockaddr_in serverAddress;
  serverAddress.sin_family = AF_INET;
  serverAddress.sin_addr.s_addr = INADDR_ANY;
  serverAddress.sin_port = htons(3000);

  if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR) {
    std::cerr << "Port 3000 is already in use.\n";
    closesocket(serverSocket);
    WSACleanup();
    sqlite3_close(database);
    DeleteCriticalSection(&appLock);
    return 1;
  }

  listen(serverSocket, SOMAXCONN);
  std::cout << "Smart Resource Exchange running on http://localhost:3000\n";
  std::cout << "SQLite database: " << DATABASE_FILE << "\n";

  while (true) {
    SOCKET client = accept(serverSocket, nullptr, nullptr);
    if (client != INVALID_SOCKET) {
      HANDLE threadHandle = CreateThread(nullptr, 0, clientThread, reinterpret_cast<LPVOID>(client), 0, nullptr);
      if (threadHandle) {
        CloseHandle(threadHandle);
      } else {
        handleClient(client);
      }
    }
  }

  closesocket(serverSocket);
  WSACleanup();
  sqlite3_close(database);
  DeleteCriticalSection(&appLock);
  return 0;
}
