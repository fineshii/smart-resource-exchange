#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iostream>
#include <queue>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

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

const std::string DATA_DIR = "data";
const std::string RESOURCES_FILE = "data/resources.db";
const std::string OFFERS_FILE = "data/offers.db";
const std::string INTERNSHIPS_FILE = "data/internships.db";

std::string encodeField(const std::string& value) {
  std::ostringstream out;
  const char* hex = "0123456789ABCDEF";
  for (unsigned char ch : value) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.') {
      out << ch;
    } else if (ch == ' ') {
      out << '+';
    } else {
      out << '%' << hex[ch >> 4] << hex[ch & 15];
    }
  }
  return out.str();
}

int hexValue(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  return 0;
}

std::string decodeField(const std::string& value) {
  std::ostringstream out;
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '+') {
      out << ' ';
    } else if (value[i] == '%' && i + 2 < value.size()) {
      out << static_cast<char>((hexValue(value[i + 1]) << 4) + hexValue(value[i + 2]));
      i += 2;
    } else {
      out << value[i];
    }
  }
  return out.str();
}

std::vector<std::string> splitLine(const std::string& line) {
  std::vector<std::string> parts;
  std::string part;
  std::istringstream stream(line);
  while (std::getline(stream, part, '|')) {
    parts.push_back(part);
  }
  return parts;
}

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

void saveDatabase();

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
    changed = true;
  }

  if (changed) {
    saveDatabase();
  }

  return changed;
}

void seedData() {
  resources.clear();
  internships.clear();
  std::time_t now = std::time(nullptr);

  resources.push_back({1, "DAA Reference Book", "Book", "Aarav", "Cormen-style algorithms book for exam preparation and project viva practice.", "High", "Exchange", now + 90, {}, "", 0});
  resources.push_back({2, "Scientific Calculator", "Calculator", "Meera", "Casio calculator available for the next lab cycle.", "Medium", "Bidding", now + 160, {}, "", 0});
  resources.push_back({3, "Physics Lab Manual Notes", "Notes", "Rohan", "Clean handwritten readings and experiment observations for revision.", "Low", "Exchange", now + 230, {}, "", 0});

  resources[0].offers.push_back({1, "Nisha", 16, "High", "Exchange", 0, calculatePriorityScore(16, "High", 0, "Exchange"), now - 40});
  resources[0].offers.push_back({1, "Kabir", 22, "Medium", "Exchange", 0, calculatePriorityScore(22, "Medium", 0, "Exchange"), now - 20});
  resources[1].offers.push_back({2, "Isha", 11, "Medium", "Bidding", 45, calculatePriorityScore(11, "Medium", 45, "Bidding"), now - 15});

  internships.push_back({"Tata Consultancy Services", "Campus internship applications", "2026-06-05"});
  internships.push_back({"Infosys Springboard", "Scholarship and certification track", "2026-06-12"});
  internships.push_back({"AICTE", "Virtual internship program", "2026-06-20"});
}

void saveDatabase() {
  CreateDirectoryA(DATA_DIR.c_str(), nullptr);

  std::ofstream resourceFile(RESOURCES_FILE.c_str(), std::ios::trunc);
  for (const Resource& resource : resources) {
    resourceFile
      << resource.id << "|"
      << encodeField(resource.title) << "|"
      << encodeField(resource.type) << "|"
      << encodeField(resource.owner) << "|"
      << encodeField(resource.description) << "|"
      << encodeField(resource.urgency) << "|"
      << encodeField(resource.mode) << "|"
      << static_cast<long long>(resource.deadline) << "|"
      << encodeField(resource.allocatedTo) << "|"
      << resource.bestScore << "\n";
  }

  std::ofstream offerFile(OFFERS_FILE.c_str(), std::ios::trunc);
  for (const Resource& resource : resources) {
    for (const Offer& offer : resource.offers) {
      offerFile
        << offer.resourceId << "|"
        << encodeField(offer.studentName) << "|"
        << offer.credits << "|"
        << encodeField(offer.urgency) << "|"
        << encodeField(offer.mode) << "|"
        << offer.bidValue << "|"
        << offer.priorityScore << "|"
        << static_cast<long long>(offer.timestamp) << "\n";
    }
  }

  std::ofstream internshipFile(INTERNSHIPS_FILE.c_str(), std::ios::trunc);
  for (const Internship& item : internships) {
    internshipFile
      << encodeField(item.company) << "|"
      << encodeField(item.title) << "|"
      << encodeField(item.deadline) << "\n";
  }
}

bool loadDatabase() {
  std::ifstream resourceFile(RESOURCES_FILE.c_str());
  if (!resourceFile) {
    return false;
  }

  resources.clear();
  internships.clear();

  std::string line;
  while (std::getline(resourceFile, line)) {
    std::vector<std::string> parts = splitLine(line);
    if (parts.size() < 10) {
      continue;
    }

    Resource resource;
    resource.id = std::stoi(parts[0]);
    resource.title = decodeField(parts[1]);
    resource.type = decodeField(parts[2]);
    resource.owner = decodeField(parts[3]);
    resource.description = decodeField(parts[4]);
    resource.urgency = decodeField(parts[5]);
    resource.mode = decodeField(parts[6]);
    resource.deadline = static_cast<std::time_t>(std::stoll(parts[7]));
    resource.allocatedTo = decodeField(parts[8]);
    resource.bestScore = std::stoi(parts[9]);
    resources.push_back(resource);
  }

  std::ifstream offerFile(OFFERS_FILE.c_str());
  while (offerFile && std::getline(offerFile, line)) {
    std::vector<std::string> parts = splitLine(line);
    if (parts.size() < 8) {
      continue;
    }

    Offer offer;
    offer.resourceId = std::stoi(parts[0]);
    offer.studentName = decodeField(parts[1]);
    offer.credits = std::stoi(parts[2]);
    offer.urgency = decodeField(parts[3]);
    offer.mode = decodeField(parts[4]);
    offer.bidValue = std::stoi(parts[5]);
    offer.priorityScore = std::stoi(parts[6]);
    offer.timestamp = static_cast<std::time_t>(std::stoll(parts[7]));

    auto it = std::find_if(resources.begin(), resources.end(), [&offer](const Resource& resource) {
      return resource.id == offer.resourceId;
    });
    if (it != resources.end()) {
      it->offers.push_back(offer);
    }
  }

  std::ifstream internshipFile(INTERNSHIPS_FILE.c_str());
  while (internshipFile && std::getline(internshipFile, line)) {
    std::vector<std::string> parts = splitLine(line);
    if (parts.size() < 3) {
      continue;
    }
    internships.push_back({decodeField(parts[0]), decodeField(parts[1]), decodeField(parts[2])});
  }

  return !resources.empty();
}

std::string resourcesJson() {
  allocateExpiredResources();
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

  allocateExpiredResources();
  if (!it->allocatedTo.empty() || std::time(nullptr) >= it->deadline) {
    statusCode = 409;
    return "{\"error\":\"Deadline has passed. This resource is already closed.\"}";
  }

  int score = calculatePriorityScore(credits, urgency, bidValue, mode);
  it->offers.push_back({resourceId, studentName, credits, urgency, mode, bidValue, score, std::time(nullptr)});
  saveDatabase();

  statusCode = 201;
  std::ostringstream response;
  response << "{\"ok\":true,\"score\":" << score << "}";
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
    responseBody = resourcesJson();
  } else if (method == "POST" && path == "/api/offers") {
    responseBody = submitOffer(body, statusCode);
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
  if (!loadDatabase()) {
    seedData();
    saveDatabase();
  }

  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    std::cerr << "Winsock startup failed.\n";
    return 1;
  }

  SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (serverSocket == INVALID_SOCKET) {
    std::cerr << "Could not create server socket.\n";
    WSACleanup();
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
    return 1;
  }

  listen(serverSocket, SOMAXCONN);
  std::cout << "Smart Resource Exchange running on http://localhost:3000\n";

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
  return 0;
}
