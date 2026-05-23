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
  int resourceId;
  std::string studentName;
  int credits;
  std::string urgency;
  std::string mode;
  int bidValue;
  int priorityScore;
  std::time_t timestamp;
};

struct Resource {
  int id;
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

std::vector<Resource> resources;
std::vector<Internship> internships;
sqlite3* database = nullptr;
CRITICAL_SECTION appLock;

const std::string DATA_DIR = "data";
const std::string DATABASE_FILE = "data/smart_resource_exchange.sqlite";

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

void bindText(sqlite3_stmt* statement, int index, const std::string& value) {
  sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

bool initDatabase() {
  CreateDirectoryA(DATA_DIR.c_str(), nullptr);
  if (sqlite3_open(DATABASE_FILE.c_str(), &database) != SQLITE_OK) {
    std::cerr << "Could not open SQLite database.\n";
    return false;
  }

  return executeSql(
    "PRAGMA foreign_keys = ON;"
    "CREATE TABLE IF NOT EXISTS resources ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
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
    "  student_name TEXT NOT NULL,"
    "  credits INTEGER NOT NULL,"
    "  urgency TEXT NOT NULL,"
    "  mode TEXT NOT NULL,"
    "  bid_value INTEGER DEFAULT 0,"
    "  priority_score INTEGER NOT NULL,"
    "  timestamp INTEGER NOT NULL,"
    "  FOREIGN KEY(resource_id) REFERENCES resources(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS internships ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  company TEXT NOT NULL,"
    "  title TEXT NOT NULL,"
    "  deadline TEXT NOT NULL"
    ");"
  );
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

void insertResource(const std::string& title, const std::string& type, const std::string& owner,
                    const std::string& description, const std::string& urgency,
                    const std::string& mode, std::time_t deadline) {
  sqlite3_stmt* statement = nullptr;
  sqlite3_prepare_v2(database,
    "INSERT INTO resources(title, type, owner, description, urgency, mode, deadline, allocated_to, best_score) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, '', 0)",
    -1, &statement, nullptr);
  bindText(statement, 1, title);
  bindText(statement, 2, type);
  bindText(statement, 3, owner);
  bindText(statement, 4, description);
  bindText(statement, 5, urgency);
  bindText(statement, 6, mode);
  sqlite3_bind_int64(statement, 7, static_cast<sqlite3_int64>(deadline));
  sqlite3_step(statement);
  sqlite3_finalize(statement);
}

void insertOffer(int resourceId, const std::string& studentName, int credits, const std::string& urgency,
                 const std::string& mode, int bidValue, int priorityScore, std::time_t timestamp) {
  sqlite3_stmt* statement = nullptr;
  sqlite3_prepare_v2(database,
    "INSERT INTO offers(resource_id, student_name, credits, urgency, mode, bid_value, priority_score, timestamp) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
    -1, &statement, nullptr);
  sqlite3_bind_int(statement, 1, resourceId);
  bindText(statement, 2, studentName);
  sqlite3_bind_int(statement, 3, credits);
  bindText(statement, 4, urgency);
  bindText(statement, 5, mode);
  sqlite3_bind_int(statement, 6, bidValue);
  sqlite3_bind_int(statement, 7, priorityScore);
  sqlite3_bind_int64(statement, 8, static_cast<sqlite3_int64>(timestamp));
  sqlite3_step(statement);
  sqlite3_finalize(statement);
}

void seedData() {
  std::time_t now = std::time(nullptr);

  insertResource("DAA Reference Book", "Book", "Aarav",
    "Algorithms textbook available for two weeks. Best for exam preparation and project viva practice.",
    "High", "Exchange", now + 90);
  insertResource("Scientific Calculator", "Calculator", "Meera",
    "Casio scientific calculator available for the next lab cycle. Clean condition with working battery.",
    "Medium", "Bidding", now + 160);
  insertResource("Physics Lab Manual Notes", "Notes", "Rohan",
    "Clean handwritten readings and experiment observations for revision before practicals.",
    "Low", "Exchange", now + 230);

  insertOffer(1, "Nisha", 16, "High", "Exchange", 0, calculatePriorityScore(16, "High", 0, "Exchange"), now - 40);
  insertOffer(1, "Kabir", 22, "Medium", "Exchange", 0, calculatePriorityScore(22, "Medium", 0, "Exchange"), now - 20);
  insertOffer(2, "Isha", 11, "Medium", "Bidding", 45, calculatePriorityScore(11, "Medium", 45, "Bidding"), now - 15);

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

void loadData() {
  resources.clear();
  internships.clear();

  sqlite3_stmt* statement = nullptr;
  sqlite3_prepare_v2(database,
    "SELECT id, title, type, owner, description, urgency, mode, deadline, allocated_to, best_score "
    "FROM resources ORDER BY allocated_to = '', deadline ASC, id ASC",
    -1, &statement, nullptr);

  while (sqlite3_step(statement) == SQLITE_ROW) {
    Resource resource;
    resource.id = sqlite3_column_int(statement, 0);
    resource.title = sqliteText(statement, 1);
    resource.type = sqliteText(statement, 2);
    resource.owner = sqliteText(statement, 3);
    resource.description = sqliteText(statement, 4);
    resource.urgency = sqliteText(statement, 5);
    resource.mode = sqliteText(statement, 6);
    resource.deadline = static_cast<std::time_t>(sqlite3_column_int64(statement, 7));
    resource.allocatedTo = sqliteText(statement, 8);
    resource.bestScore = sqlite3_column_int(statement, 9);
    resources.push_back(resource);
  }
  sqlite3_finalize(statement);

  sqlite3_prepare_v2(database,
    "SELECT resource_id, student_name, credits, urgency, mode, bid_value, priority_score, timestamp "
    "FROM offers ORDER BY timestamp ASC",
    -1, &statement, nullptr);

  while (sqlite3_step(statement) == SQLITE_ROW) {
    Offer offer;
    offer.resourceId = sqlite3_column_int(statement, 0);
    offer.studentName = sqliteText(statement, 1);
    offer.credits = sqlite3_column_int(statement, 2);
    offer.urgency = sqliteText(statement, 3);
    offer.mode = sqliteText(statement, 4);
    offer.bidValue = sqlite3_column_int(statement, 5);
    offer.priorityScore = sqlite3_column_int(statement, 6);
    offer.timestamp = static_cast<std::time_t>(sqlite3_column_int64(statement, 7));

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
      heap.push(offer);
    }

    Offer winner = heap.top();
    resource.allocatedTo = winner.studentName;
    resource.bestScore = winner.priorityScore;
    updateAllocation(resource.id, winner.studentName, winner.priorityScore);
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

  json << "],\"internships\":[";
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
  std::string studentName = parseStringField(body, "studentName");
  int credits = parseIntField(body, "credits");
  std::string urgency = parseStringField(body, "urgency");
  std::string mode = parseStringField(body, "mode");
  int bidValue = parseIntField(body, "bidValue");

  if (studentName.empty() || resourceId <= 0) {
    statusCode = 400;
    return "{\"error\":\"Student name and resource are required.\"}";
  }

  auto it = std::find_if(resources.begin(), resources.end(), [resourceId](const Resource& resource) {
    return resource.id == resourceId;
  });

  if (it == resources.end()) {
    statusCode = 404;
    return "{\"error\":\"Resource not found.\"}";
  }

  if (!it->allocatedTo.empty() || std::time(nullptr) >= it->deadline) {
    statusCode = 409;
    return "{\"error\":\"Deadline has passed. This resource is already closed.\"}";
  }

  int score = calculatePriorityScore(credits, urgency, bidValue, mode);
  insertOffer(resourceId, studentName, credits, urgency, mode, bidValue, score, std::time(nullptr));

  statusCode = 201;
  std::ostringstream response;
  response << "{\"ok\":true,\"score\":" << score << "}";
  return response.str();
}

std::string createResource(const std::string& body, int& statusCode) {
  std::string title = parseStringField(body, "title");
  std::string type = parseStringField(body, "type");
  std::string owner = parseStringField(body, "owner");
  std::string description = parseStringField(body, "description");
  std::string urgency = parseStringField(body, "urgency");
  std::string mode = parseStringField(body, "mode");
  int durationMinutes = parseIntField(body, "durationMinutes", 180);

  if (title.empty() || type.empty() || owner.empty() || description.empty()) {
    statusCode = 400;
    return "{\"error\":\"Title, type, owner, and description are required.\"}";
  }

  if (urgency != "High" && urgency != "Medium" && urgency != "Low") {
    urgency = "Medium";
  }
  if (mode != "Exchange" && mode != "Bidding") {
    mode = "Exchange";
  }
  if (durationMinutes < 5) {
    durationMinutes = 5;
  }

  std::time_t deadline = std::time(nullptr) + (durationMinutes * 60);
  insertResource(title, type, owner, description, urgency, mode, deadline);

  statusCode = 201;
  std::ostringstream response;
  response << "{\"ok\":true,\"id\":" << sqlite3_last_insert_rowid(database) << "}";
  return response.str();
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
                       statusCode == 404 ? "Not Found" :
                       statusCode == 409 ? "Conflict" : "Server Error";
  std::ostringstream response;
  response << "HTTP/1.1 " << statusCode << " " << reason << "\r\n"
           << "Content-Type: " << contentType << "\r\n"
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
