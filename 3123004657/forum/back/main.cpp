#include <httplib.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <sqlite3.h>
#include <string>
#include <vector>
sqlite3 *db = nullptr;
// 生成随机 session_id
std::string generate_session_id() {
  static std::mt19937 rng{std::random_device{}()};
  static std::uniform_int_distribution<> dist(0, 15);
  std::string id;
  for (int i = 0; i < 32; ++i)
    id += "0123456789abcdef"[dist(rng)];
  return id;
}
// 用户接口
namespace UserAPI {
// POST /api/auth/login - 用户登录
void handleLogin(const httplib::Request &req, httplib::Response &res) {
  // json:{"account":"xxx","password":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string password = body["password"];
  char *err_msg = nullptr;

  std::string sql = "SELECT password, is_banned, is_admin, last_login_attempt, "
                    "last_login_time FROM users WHERE account='" +
                    account + "';";
  struct LoginData {
    std::string db_password;
    bool is_banned;
    bool is_admin;
    int last_login_attempt;
    int last_login_time;
    bool found;
  };
  LoginData login_data;
  login_data.db_password = "";
  login_data.is_banned = false;
  login_data.is_admin = false;
  login_data.last_login_attempt = 0;
  login_data.last_login_time = 0;
  login_data.found = false;

  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *login_data = static_cast<LoginData *>(data);
        login_data->db_password = argv[0] ? argv[0] : "";
        login_data->is_banned = argv[1] ? std::string(argv[1]) == "1" : false;
        login_data->is_admin = argv[2] ? std::string(argv[2]) == "1" : false;
        login_data->last_login_attempt = argv[3] ? std::stoi(argv[3]) : 0;
        login_data->last_login_time = argv[4] ? std::stoi(argv[4]) : 0;
        login_data->found = true;
        return 0;
      },
      &login_data, &err_msg);

  nlohmann::json response_json;
  int now = static_cast<int>(time(nullptr));
  if (rc != SQLITE_OK) {
    std::cerr << "login_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Login error";
    res.status = 500;
  } else if (!login_data.found) {
    response_json["status"] = "failure";
    response_json["message"] = "User not found";
    res.status = 404;
  } else if (login_data.is_banned) {
    response_json["status"] = "failure";
    response_json["message"] = "User is banned";
    res.status = 403;
  } else if (now - login_data.last_login_attempt < 5) {
    int wait_seconds = 5 - (now - login_data.last_login_attempt);
    response_json["status"] = "failure";
    response_json["message"] = "Please wait before next login attempt";
    response_json["wait_seconds"] = wait_seconds;
    res.status = 429;
  } else {
    // 超过5秒，无论密码对错都要更新时间
    std::string update_sql =
        "UPDATE users SET last_login_attempt=" + std::to_string(now) +
        ", last_login_time=" + std::to_string(now) + " WHERE account='" +
        account + "';";
    sqlite3_exec(db, update_sql.c_str(), nullptr, nullptr, nullptr);

    if (login_data.db_password == password) {
      // 登录成功，更新 is_online
      std::string online_sql =
          "UPDATE users SET is_online=1 WHERE account='" + account + "';";
      rc = sqlite3_exec(db, online_sql.c_str(), nullptr, nullptr, &err_msg);

      response_json["first_login"] = (login_data.last_login_time == 0);

      if (rc != SQLITE_OK) {
        std::cerr << "update_online_error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        response_json["status"] = "failure";
        response_json["message"] =
            "Login successful but failed to update online status";
        res.status = 500;
      } else {
        // 生成 session_id
        std::string session_id = generate_session_id();
        int expires = static_cast<int>(time(nullptr)) + 86400; // 有效期1天

        // 写入 session 表
        std::string session_sql = "INSERT INTO sessions (session_id, account, "
                                  "is_admin, expires_at) VALUES ('" +
                                  session_id + "', '" + account + "', " +
                                  (login_data.is_admin ? "1" : "0") + ", " +
                                  std::to_string(expires) + ");";
        char *session_err = nullptr;
        int session_rc = sqlite3_exec(db, session_sql.c_str(), nullptr, nullptr,
                                      &session_err);

        if (session_rc != SQLITE_OK) {
          std::cerr << "session_error: " << session_err << std::endl;
          sqlite3_free(session_err);
          response_json["status"] = "failure";
          response_json["message"] =
              "Login successful but failed to create session";
          res.status = 500;
        } else {
          // 设置 cookie，有效期1天
          res.set_header("Set-Cookie", "session_id=" + session_id +
                                           "; Path=/; Max-Age=86400; HttpOnly");
          res.set_header("Set-Cookie", "account=" + account +
                                           "; Path=/; Max-Age=86400; HttpOnly");

          response_json["status"] = "success";
          response_json["message"] = "Login successful";
          response_json["is_admin"] = login_data.is_admin;
          response_json["session_id"] = session_id;
          res.status = 200;
        }
      }
    } else {
      // 密码错误
      response_json["status"] = "failure";
      response_json["message"] = "Invalid credentials";
      res.status = 401;
    }
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/auth/logout - 用户登出
void handleLogout(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found";
    res.status = 400;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 查询 session_id 对应账号
  std::string query_sql =
      "SELECT account FROM sessions WHERE session_id='" + session_id + "';";
  std::string session_account;
  char *err_msg = nullptr;
  int rc = sqlite3_exec(
      db, query_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_account = static_cast<std::string *>(data);
        *session_account = argv[0] ? argv[0] : "";
        return 0;
      },
      &session_account, &err_msg);

  // 1. 查询出错
  if (rc != SQLITE_OK) {
    std::cerr << "logout_query_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Logout failed (session query error)";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 2. session_id不存在
  if (session_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 3. session_id不属于当前账号
  if (session_account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session does not belong to this account";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 删除 session 表中的 session_id
  std::string sql =
      "DELETE FROM sessions WHERE session_id='" + session_id + "';";
  rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  // 4. 删除失败
  if (rc != SQLITE_OK) {
    std::cerr << "logout_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Logout failed (delete session error)";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 5. 成功
  res.set_header("Set-Cookie",
                 "session_id=deleted; Path=/; Max-Age=0; HttpOnly");
  res.set_header("Set-Cookie", "account=deleted; Path=/; Max-Age=0; HttpOnly");
  response_json["status"] = "success";
  response_json["message"] = "Logout successful";
  res.status = 200;
  res.set_content(response_json.dump(), "application/json");
}
// GET /api/users/id - 获取用户信息
void handleGetUserInfo(const httplib::Request &req, httplib::Response &res) {
  // 1. 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 2. 用 session_id 查数据库获取账号和有效期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  // 分开判断
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 3. 用查到的账号查询用户信息
  std::string user_account = session_check.account;
  std::string sql =
      "SELECT account, username, bio, major, grade, role, is_admin, "
      "is_online, last_login_time FROM users WHERE account='" +
      user_account + "';";
  char *err_msg = nullptr;
  struct UserInfo {
    std::string account;
    std::string username;
    std::string bio;
    std::string major;
    std::string grade;
    std::string role;
    bool is_admin;
    bool is_online;
    int last_login_time;
    bool found;
  };
  UserInfo user_info;
  user_info.found = false;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *user_info = static_cast<UserInfo *>(data);
        user_info->account = argv[0] ? argv[0] : "";
        user_info->username = argv[1] ? argv[1] : "";
        user_info->bio = argv[2] ? argv[2] : "";
        user_info->major = argv[3] ? argv[3] : "";
        user_info->grade = argv[4] ? argv[4] : "";
        user_info->role = argv[5] ? argv[5] : "";
        user_info->is_admin = argv[6] ? std::string(argv[6]) == "1" : false;
        user_info->is_online = argv[7] ? std::string(argv[7]) == "1" : false;
        user_info->last_login_time = argv[8] ? std::stoi(argv[8]) : 0;
        user_info->found = true;
        return 0;
      },
      &user_info, &err_msg);

  // 分开判断
  if (rc != SQLITE_OK) {
    std::cerr << "get_user_info_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve user info";
    res.status = 500;
  } else if (!user_info.found) {
    response_json["status"] = "failure";
    response_json["message"] = "User not found";
    res.status = 404;
  } else {
    response_json["status"] = "success";
    response_json["data"] = {{"account", user_info.account},
                             {"username", user_info.username},
                             {"bio", user_info.bio},
                             {"major", user_info.major},
                             {"grade", user_info.grade},
                             {"role", user_info.role},
                             {"is_admin", user_info.is_admin},
                             {"is_online", user_info.is_online},
                             {"last_login_time", user_info.last_login_time}};
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// PUT /api/users/id - 修改用户名，个人简介，专业信息
void handleUpdateUserInfo(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  // 分开判断
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 只允许修改自己的信息
  auto body = nlohmann::json::parse(req.body);
  std::string username = body.value("username", "");
  std::string bio = body.value("bio", "");

  if (username.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "Username cannot be empty";
    res.status = 400;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string sql = "UPDATE users SET username='" + username + "', bio='" +
                    bio + "' WHERE account='" + cookie_account + "';";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "update_user_info_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to update user info";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] = "User info updated successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// PUT /api/users/id/password - 修改密码
void handleChangePassword(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  // 分开判断
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 只允许修改自己的密码
  auto body = nlohmann::json::parse(req.body);
  std::string old_password = body["old_password"];
  std::string new_password = body["new_password"];

  std::string sql =
      "SELECT password FROM users WHERE account='" + cookie_account + "';";
  char *err_msg = nullptr;
  struct PasswordData {
    std::string db_password;
    bool found = false;
  };
  PasswordData password_data;
  password_data.db_password = "";
  password_data.found = false;

  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *password_data = static_cast<PasswordData *>(data);
        password_data->db_password = argv[0] ? argv[0] : "";
        password_data->found = true;
        return 0;
      },
      &password_data, &err_msg);

  // 分开判断
  if (rc != SQLITE_OK) {
    std::cerr << "change_password_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to change password";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!password_data.found) {
    response_json["status"] = "failure";
    response_json["message"] = "User not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (password_data.db_password != old_password) {
    response_json["status"] = "failure";
    response_json["message"] = "Old password is incorrect";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string update_sql = "UPDATE users SET password='" + new_password +
                           "' WHERE account='" + cookie_account + "';";
  rc = sqlite3_exec(db, update_sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "update_password_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to update password";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] = "Password changed successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// GET /api/users/id/posts - 获取用户发布的帖子
void handleGetUserPosts(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  // 分开判断
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 用 cookie_account 查询该用户的帖子
  std::string sql =
      "SELECT p.post_id, p.title, SUBSTR(p.content, 1, 100) as summary, "
      "u.username, u.grade, u.major, u.role, p.category, p.is_anonymous, "
      "p.view_count, p.like_count, p.is_top, p.comment_count, p.created_at "
      "FROM posts p "
      "LEFT JOIN users u ON p.account = u.account "
      "WHERE p.account='" +
      cookie_account +
      "' AND p.is_deleted=0 "
      "ORDER BY p.created_at DESC;";

  char *err_msg = nullptr;
  struct PostInfo {
    int post_id;
    std::string title;
    std::string summary;
    std::string username;
    std::string grade;
    std::string major;
    std::string role;
    std::string category;
    int is_anonymous;
    int view_count;
    int like_count;
    bool is_top;
    int comment_count;
    std::string created_at;
  };
  std::vector<PostInfo> posts;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *posts = static_cast<std::vector<PostInfo> *>(data);
        PostInfo post;
        post.post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post.title = argv[1] ? argv[1] : "";
        post.summary = argv[2] ? argv[2] : "";
        post.username = argv[3] ? argv[3] : "";
        post.grade = argv[4] ? argv[4] : "";
        post.major = argv[5] ? argv[5] : "";
        post.role = argv[6] ? argv[6] : "";
        post.category = argv[7] ? argv[7] : "";
        post.is_anonymous = argv[8] ? std::stoi(argv[8]) : 0;
        post.view_count = argv[9] ? std::stoi(argv[9]) : 0;
        post.like_count = argv[10] ? std::stoi(argv[10]) : 0;
        post.is_top = argv[11] ? std::string(argv[11]) == "1" : false;
        post.comment_count = argv[12] ? std::stoi(argv[12]) : 0;
        post.created_at = argv[13] ? argv[13] : "";
        posts->push_back(post);
        return 0;
      },
      &posts, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "get_user_posts_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve user posts";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json posts_json = nlohmann::json::array();
    for (const auto &post : posts) {
      std::string display_username =
          post.is_anonymous ? "匿名用户" : post.username;
      posts_json.push_back({{"post_id", post.post_id},
                            {"username", display_username},
                            {"title", post.title},
                            {"summary", post.summary},
                            {"grade", post.grade},
                            {"major", post.major},
                            {"role", post.role},
                            {"category", post.category},
                            {"is_anonymous", post.is_anonymous},
                            {"view_count", post.view_count},
                            {"like_count", post.like_count},
                            {"is_top", post.is_top},
                            {"comment_count", post.comment_count},
                            {"created_at", post.created_at}});
    }
    response_json["data"] = posts_json;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// GET /api/users/id/posts/count 获取用户帖子数量
void handleGetUserPostCount(const httplib::Request &req,
                            httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  // 分开判断
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 用 cookie_account 查询帖子数量
  std::string sql = "SELECT COUNT(*) FROM posts WHERE account='" +
                    cookie_account + "' AND is_deleted=0;";
  char *err_msg = nullptr;
  int post_count = 0;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *post_count = static_cast<int *>(data);
        *post_count = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &post_count, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "get_user_post_count_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve user post count";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["data"] = {{"post_count", post_count}};
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// GET /api/users/id/comments - 获取用户的评论
void handleGetUserComments(const httplib::Request &req,
                           httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 用 cookie_account 查询该用户的评论
  std::string sql =
      "SELECT c.comment_id, c.post_id, c.content, c.parent_id, c.like_count, "
      "c.created_at, u.username, c.is_anonymous "
      "FROM comments c "
      "LEFT JOIN users u ON c.account = u.account "
      "WHERE c.account='" +
      cookie_account + "' AND c.is_deleted=0;";
  char *err_msg = nullptr;
  struct CommentInfo {
    int comment_id;
    int post_id;
    std::string content;
    int parent_id;
    int like_count;
    std::string created_at;
    std::string username;
    int is_anonymous;
  };
  std::vector<CommentInfo> comments;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *comments = static_cast<std::vector<CommentInfo> *>(data);
        CommentInfo comment;
        comment.comment_id = argv[0] ? std::stoi(argv[0]) : 0;
        comment.post_id = argv[1] ? std::stoi(argv[1]) : 0;
        comment.content = argv[2] ? argv[2] : "";
        comment.parent_id = argv[3] ? std::stoi(argv[3]) : -1;
        comment.like_count = argv[4] ? std::stoi(argv[4]) : 0;
        comment.created_at = argv[5] ? argv[5] : "";
        comment.username = argv[6] ? argv[6] : "";
        comment.is_anonymous = argv[7] ? std::stoi(argv[7]) : 0;
        comments->push_back(comment);
        return 0;
      },
      &comments, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "get_user_comments_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve user comments";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json comments_json = nlohmann::json::array();
    for (const auto &comment : comments) {
      std::string display_username =
          comment.is_anonymous ? "匿名用户" : comment.username;
      comments_json.push_back({{"comment_id", comment.comment_id},
                               {"post_id", comment.post_id},
                               {"content", comment.content},
                               {"parent_id", comment.parent_id},
                               {"like_count", comment.like_count},
                               {"created_at", comment.created_at},
                               {"username", display_username}});
    }
    response_json["data"] = comments_json;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// GET /api/users/id/favorites - 获取用户收藏的帖子
void handleGetUserFavorites(const httplib::Request &req,
                            httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  // 分开判断
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 用 cookie_account 查询收藏帖子
  std::string sql =
      "SELECT p.post_id, p.account, u.username, p.title, "
      "SUBSTR(p.content, 1, 100) as summary, "
      "u.grade, u.major, u.role, p.category, p.is_anonymous, "
      "p.view_count, p.like_count, p.is_top, p.comment_count, p.created_at "
      "FROM posts p "
      "JOIN user_favorites uf ON p.post_id = uf.post_id "
      "LEFT JOIN users u ON p.account = u.account "
      "WHERE uf.account='" +
      cookie_account +
      "' AND p.is_deleted=0 "
      "ORDER BY uf.created_at DESC;";

  char *err_msg = nullptr;
  struct PostInfo {
    int post_id;
    std::string account;
    std::string username;
    std::string title;
    std::string summary;
    std::string grade;
    std::string major;
    std::string role;
    std::string category;
    int is_anonymous;
    int view_count;
    int like_count;
    bool is_top;
    int comment_count;
    std::string created_at;
  };
  std::vector<PostInfo> favorites;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *favorites = static_cast<std::vector<PostInfo> *>(data);
        PostInfo post;
        post.post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post.account = argv[1] ? argv[1] : "";
        post.username = argv[2] ? argv[2] : "";
        post.title = argv[3] ? argv[3] : "";
        post.summary = argv[4] ? argv[4] : "";
        post.grade = argv[5] ? argv[5] : "";
        post.major = argv[6] ? argv[6] : "";
        post.role = argv[7] ? argv[7] : "";
        post.category = argv[8] ? argv[8] : "";
        post.is_anonymous = argv[9] ? std::stoi(argv[9]) : 0;
        post.view_count = argv[10] ? std::stoi(argv[10]) : 0;
        post.like_count = argv[11] ? std::stoi(argv[11]) : 0;
        post.is_top = argv[12] ? std::string(argv[12]) == "1" : false;
        post.comment_count = argv[13] ? std::stoi(argv[13]) : 0;
        post.created_at = argv[14] ? argv[14] : "";
        favorites->push_back(post);
        return 0;
      },
      &favorites, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "get_user_favorites_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve user favorites";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json favorites_json = nlohmann::json::array();
    for (const auto &post : favorites) {
      std::string display_username =
          post.is_anonymous ? "匿名用户" : post.username;
      nlohmann::json item = {{"post_id", post.post_id},
                             {"username", display_username},
                             {"title", post.title},
                             {"summary", post.summary},
                             {"grade", post.grade},
                             {"major", post.major},
                             {"role", post.role},
                             {"category", post.category},
                             {"is_anonymous", post.is_anonymous},
                             {"view_count", post.view_count},
                             {"like_count", post.like_count},
                             {"is_top", post.is_top},
                             {"comment_count", post.comment_count},
                             {"created_at", post.created_at}};
      if (!post.is_anonymous) {
        item["account"] = post.account;
      }
      favorites_json.push_back(item);
    }
    response_json["data"] = favorites_json;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// GET /api/users/id/likes - 获取用户点赞的帖子
void handleGetUserLikes(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  // 分开判断
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 用 cookie_account 查询点赞的帖子
  std::string sql =
      "SELECT p.post_id, p.account, u.username, p.title, "
      "SUBSTR(p.content, 1, 100) as summary, "
      "u.grade, u.major, u.role, p.category, p.is_anonymous, "
      "p.view_count, p.like_count, p.is_top, p.comment_count, p.created_at "
      "FROM posts p "
      "JOIN user_likes ul ON p.post_id = ul.post_id "
      "LEFT JOIN users u ON p.account = u.account "
      "WHERE ul.account='" +
      cookie_account +
      "' AND p.is_deleted=0 "
      "ORDER BY ul.created_at DESC;";

  char *err_msg = nullptr;
  struct PostInfo {
    int post_id;
    std::string account;
    std::string username;
    std::string title;
    std::string summary;
    std::string grade;
    std::string major;
    std::string role;
    std::string category;
    int is_anonymous;
    int view_count;
    int like_count;
    bool is_top;
    int comment_count;
    std::string created_at;
  };
  std::vector<PostInfo> likes;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *likes = static_cast<std::vector<PostInfo> *>(data);
        PostInfo post;
        post.post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post.account = argv[1] ? argv[1] : "";
        post.username = argv[2] ? argv[2] : "";
        post.title = argv[3] ? argv[3] : "";
        post.summary = argv[4] ? argv[4] : "";
        post.grade = argv[5] ? argv[5] : "";
        post.major = argv[6] ? argv[6] : "";
        post.role = argv[7] ? argv[7] : "";
        post.category = argv[8] ? argv[8] : "";
        post.is_anonymous = argv[9] ? std::stoi(argv[9]) : 0;
        post.view_count = argv[10] ? std::stoi(argv[10]) : 0;
        post.like_count = argv[11] ? std::stoi(argv[11]) : 0;
        post.is_top = argv[12] ? std::string(argv[12]) == "1" : false;
        post.comment_count = argv[13] ? std::stoi(argv[13]) : 0;
        post.created_at = argv[14] ? argv[14] : "";
        likes->push_back(post);
        return 0;
      },
      &likes, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "get_user_likes_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve user likes";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json likes_json = nlohmann::json::array();
    for (const auto &post : likes) {
      if (post.is_anonymous) {
        likes_json.push_back({{"post_id", post.post_id},
                              {"username", "匿名用户"},
                              {"title", post.title},
                              {"summary", post.summary},
                              {"grade", post.grade},
                              {"major", post.major},
                              {"role", post.role},
                              {"category", post.category},
                              {"is_anonymous", post.is_anonymous},
                              {"view_count", post.view_count},
                              {"like_count", post.like_count},
                              {"is_top", post.is_top},
                              {"comment_count", post.comment_count},
                              {"created_at", post.created_at}});
      } else {
        likes_json.push_back({{"post_id", post.post_id},
                              {"account", post.account},
                              {"username", post.username},
                              {"title", post.title},
                              {"summary", post.summary},
                              {"grade", post.grade},
                              {"major", post.major},
                              {"role", post.role},
                              {"category", post.category},
                              {"is_anonymous", post.is_anonymous},
                              {"view_count", post.view_count},
                              {"like_count", post.like_count},
                              {"is_top", post.is_top},
                              {"comment_count", post.comment_count},
                              {"created_at", post.created_at}});
      }
    }
    response_json["data"] = likes_json;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
//  GET /api/notifications - 获取用户的通知列表
void handleGetNotifications(const httplib::Request &req,
                            httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  // 分开判断
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 查询通知
  std::string sql = "SELECT n.notification_id, n.sender_account, u.username, "
                    "n.type, n.related_id, n.content, n.is_read, n.created_at "
                    "FROM notifications n "
                    "LEFT JOIN users u ON n.sender_account = u.account "
                    "WHERE n.receiver_account='" +
                    cookie_account +
                    "' "
                    "ORDER BY n.created_at DESC;";

  char *err_msg = nullptr;
  struct NotificationInfo {
    int notification_id;
    std::string sender_account;
    std::string sender_username;
    std::string type;
    int related_id;
    std::string content;
    bool is_read;
    std::string created_at;
  };
  std::vector<NotificationInfo> notifications;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *notifications =
            static_cast<std::vector<NotificationInfo> *>(data);
        NotificationInfo notification;
        notification.notification_id = argv[0] ? std::stoi(argv[0]) : 0;
        notification.sender_account = argv[1] ? argv[1] : "";
        notification.sender_username = argv[2] ? argv[2] : "";
        notification.type = argv[3] ? argv[3] : "";
        notification.related_id = argv[4] ? std::stoi(argv[4]) : 0;
        notification.content = argv[5] ? argv[5] : "";
        notification.is_read = argv[6] ? std::string(argv[6]) == "1" : false;
        notification.created_at = argv[7] ? argv[7] : "";
        notifications->push_back(notification);
        return 0;
      },
      &notifications, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "get_notifications_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve notifications";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json notifications_json = nlohmann::json::array();
    for (const auto &notification : notifications) {
      notifications_json.push_back(
          {{"notification_id", notification.notification_id},
           {"sender_account", notification.sender_account},
           {"sender_username", notification.sender_username},
           {"type", notification.type},
           {"related_id", notification.related_id},
           {"content", notification.content},
           {"is_read", notification.is_read},
           {"created_at", notification.created_at}});
    }
    response_json["data"] = notifications_json;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// PUT /api/notifications/id/read - 标记通知为已读
void handleMarkAsRead(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  // 分开判断
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 标记通知为已读
  auto body = nlohmann::json::parse(req.body);
  int notification_id = body["notification_id"];
  std::string sql =
      "UPDATE notifications SET is_read=1 WHERE notification_id=" +
      std::to_string(notification_id) + " AND receiver_account='" +
      cookie_account + "';";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "mark_as_read_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to mark notification as read";
    res.status = 500;
  } else if (sqlite3_changes(db) == 0) {
    response_json["status"] = "failure";
    response_json["message"] = "Notification not found";
    res.status = 404;
  } else {
    response_json["status"] = "success";
    response_json["message"] = "Notification marked as read";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// PUT /api/notifications/read-all - 标记所有通知为已读
void handleMarkAllAsRead(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  // 分开判断
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 标记所有通知为已读
  std::string sql =
      "UPDATE notifications SET is_read=1 WHERE receiver_account='" +
      cookie_account + "' AND is_read=0;";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "mark_all_as_read_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to mark all notifications as read";
    res.status = 500;
  } else {
    int affected = sqlite3_changes(db);
    response_json["status"] = "success";
    response_json["message"] = "All notifications marked as read";
    response_json["affected_count"] = affected;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// GET /api/notifications/unread-count - 获取未读通知数量
void handleGetUnreadCount(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  // 分开判断
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 查询未读通知数量
  std::string sql =
      "SELECT COUNT(*) FROM notifications WHERE receiver_account='" +
      cookie_account + "' AND is_read=0;";
  char *err_msg = nullptr;
  int unread_count = 0;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *unread_count = static_cast<int *>(data);
        *unread_count = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &unread_count, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "get_unread_count_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve unread notification count";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["data"] = {{"unread_count", unread_count}};
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// DELETE /api/notifications/read - 删除所有已读通知
void handleDeleteAllReadNotifications(const httplib::Request &req,
                                      httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  // 分开判断
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 删除所有已读通知，只允许本人操作
  std::string sql = "DELETE FROM notifications WHERE receiver_account='" +
                    cookie_account + "' AND is_read=1;";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "delete_all_read_notifications_error: " << err_msg
              << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to delete all read notifications";
    res.status = 500;
  } else {
    int affected = sqlite3_changes(db);
    response_json["status"] = "success";
    response_json["message"] = "All read notifications deleted";
    response_json["affected_count"] = affected;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// DELETE /api/notifications/id - 删除某个通知
void handleDeleteNotification(const httplib::Request &req,
                              httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  // 分开判断
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 只允许本人删除自己的通知
  auto body = nlohmann::json::parse(req.body);
  int notification_id = body["notification_id"];
  std::string sql = "DELETE FROM notifications WHERE notification_id=" +
                    std::to_string(notification_id) +
                    " AND receiver_account='" + cookie_account + "';";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "delete_notification_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to delete notification";
    res.status = 500;
  } else if (sqlite3_changes(db) == 0) {
    response_json["status"] = "failure";
    response_json["message"] = "Notification not found";
    res.status = 404;
  } else {
    response_json["status"] = "success";
    response_json["message"] = "Notification deleted successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
//  POST /api/users/basic-info - 获取用户基本信息
void handleGetUserBasicInfo(const httplib::Request &req,
                            httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  std::string account = body.value("account", "");
  nlohmann::json response_json;
  if (account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "Account is required";
    res.status = 400;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string sql = "SELECT account, username, bio, major, grade, role FROM "
                    "users WHERE account='" +
                    account + "';";
  char *err_msg = nullptr;
  struct UserBasicInfo {
    std::string account;
    std::string username;
    std::string bio;
    std::string major;
    std::string grade;
    std::string role;
    bool found = false;
  };
  UserBasicInfo info;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *info = static_cast<UserBasicInfo *>(data);
        info->account = argv[0] ? argv[0] : "";
        info->username = argv[1] ? argv[1] : "";
        info->bio = argv[2] ? argv[2] : "";
        info->major = argv[3] ? argv[3] : "";
        info->grade = argv[4] ? argv[4] : "";
        info->role = argv[5] ? argv[5] : "";
        info->found = true;
        return 0;
      },
      &info, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "get_user_basic_info_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve user info";
    res.status = 500;
  } else if (!info.found) {
    response_json["status"] = "failure";
    response_json["message"] = "User not found";
    res.status = 404;
  } else {
    response_json["status"] = "success";
    response_json["data"] = {
        {"account", info.account}, {"username", info.username},
        {"bio", info.bio},         {"major", info.major},
        {"grade", info.grade},     {"role", info.role}};
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
//  GET /api/users/id/comment-likes - 获取用户点赞的评论
void handleGetUserCommentLikes(const httplib::Request &req,
                               httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  // 分开判断
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 查询用户点赞的评论，只允许本人查询
  std::string sql = "SELECT c.comment_id, c.post_id, c.content, "
                    "COALESCE(c.parent_id, 0) as parent_id, "
                    "c.like_count, c.created_at, u.username, c.is_anonymous "
                    "FROM comments c "
                    "JOIN comment_likes cl ON c.comment_id = cl.comment_id "
                    "LEFT JOIN users u ON c.account = u.account "
                    "WHERE cl.account='" +
                    cookie_account +
                    "' AND c.is_deleted=0 "
                    "ORDER BY cl.created_at DESC;";

  char *err_msg = nullptr;
  struct CommentInfo {
    int comment_id;
    int post_id;
    std::string content;
    int parent_id;
    int like_count;
    std::string created_at;
    std::string username;
    int is_anonymous;
  };
  std::vector<CommentInfo> liked_comments;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *liked_comments = static_cast<std::vector<CommentInfo> *>(data);
        CommentInfo comment;
        comment.comment_id = argv[0] ? std::stoi(argv[0]) : 0;
        comment.post_id = argv[1] ? std::stoi(argv[1]) : 0;
        comment.content = argv[2] ? argv[2] : "";
        comment.parent_id = argv[3] ? std::stoi(argv[3]) : 0;
        comment.like_count = argv[4] ? std::stoi(argv[4]) : 0;
        comment.created_at = argv[5] ? argv[5] : "";
        comment.username = argv[6] ? argv[6] : "";
        comment.is_anonymous = argv[7] ? std::stoi(argv[7]) : 0;
        liked_comments->push_back(comment);
        return 0;
      },
      &liked_comments, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "get_user_comment_likes_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve liked comments";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json comments_json = nlohmann::json::array();
    for (const auto &comment : liked_comments) {
      std::string display_username =
          comment.is_anonymous ? "匿名用户" : comment.username;
      comments_json.push_back({{"comment_id", comment.comment_id},
                               {"post_id", comment.post_id},
                               {"username", display_username},
                               {"content", comment.content},
                               {"parent_id", comment.parent_id},
                               {"like_count", comment.like_count},
                               {"created_at", comment.created_at}});
    }
    response_json["data"] = comments_json;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/users/search - 搜索用户
void handleSearchUsers(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  std::string keyword = body.value("keyword", "");
  int page = body.value("page", 1);
  int page_size = body.value("page_size", 10);
  int offset = (page - 1) * page_size;

  nlohmann::json response_json;
  if (keyword.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "Search keyword is required";
    res.status = 400;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string sql =
      "SELECT account, username, bio, major, grade, role, is_admin, "
      "is_online, is_banned "
      "FROM users WHERE account LIKE '%" +
      keyword + "%' OR username LIKE '%" + keyword +
      "%' "
      "ORDER BY account ASC LIMIT " +
      std::to_string(page_size) + " OFFSET " + std::to_string(offset) + ";";
  char *err_msg = nullptr;
  struct UserInfo {
    std::string account;
    std::string username;
    std::string bio;
    std::string major;
    std::string grade;
    std::string role;
    bool is_admin;
    bool is_online;
    bool is_banned;
  };
  std::vector<UserInfo> users;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *users = static_cast<std::vector<UserInfo> *>(data);
        UserInfo user;
        user.account = argv[0] ? argv[0] : "";
        user.username = argv[1] ? argv[1] : "";
        user.bio = argv[2] ? argv[2] : "";
        user.major = argv[3] ? argv[3] : "";
        user.grade = argv[4] ? argv[4] : "";
        user.role = argv[5] ? argv[5] : "";
        user.is_admin = argv[6] ? std::string(argv[6]) == "1" : false;
        user.is_online = argv[7] ? std::string(argv[7]) == "1" : false;
        user.is_banned = argv[8] ? std::string(argv[8]) == "1" : false;
        users->push_back(user);
        return 0;
      },
      &users, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "search_users_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to search users";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json users_json = nlohmann::json::array();
    for (const auto &user : users) {
      users_json.push_back({{"account", user.account},
                            {"username", user.username},
                            {"bio", user.bio},
                            {"major", user.major},
                            {"grade", user.grade},
                            {"role", user.role},
                            {"is_admin", user.is_admin},
                            {"is_online", user.is_online},
                            {"is_banned", user.is_banned}});
    }
    response_json["data"] = users_json;
    response_json["page"] = page;
    response_json["page_size"] = page_size;
    response_json["count"] = users.size();
    response_json["keyword"] = keyword;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
} // namespace UserAPI
namespace PostAPI {
// POST /api/posts - 获取帖子列表，置顶优先，其次时间降序，支持分页和筛选
void handleGetPosts(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  int page = body.value("page", 1);
  int page_size = body.value("page_size", 10);
  int offset = (page - 1) * page_size;
  std::string major = body.value("major", "");
  std::string grade = body.value("grade", "");
  std::string role = body.value("role", "");
  std::string category = body.value("category", "");

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string sql =
      "SELECT p.post_id, p.account, u.username, p.title, SUBSTR(p.content, 1, "
      "100) as summary, "
      "u.grade, u.major, u.role, p.category, p.is_anonymous, "
      "p.view_count, p.like_count, p.is_top, p.comment_count, p.created_at "
      "FROM posts p "
      "LEFT JOIN users u ON p.account = u.account "
      "WHERE p.is_deleted=0";

  if (!major.empty())
    sql += " AND u.major='" + major + "'";
  if (!grade.empty())
    sql += " AND u.grade='" + grade + "'";
  if (!role.empty())
    sql += " AND u.role='" + role + "'";
  if (!category.empty())
    sql += " AND p.category='" + category + "'";
  sql += " ORDER BY p.is_top DESC, p.created_at DESC LIMIT " +
         std::to_string(page_size) + " OFFSET " + std::to_string(offset) + ";";

  char *err_msg = nullptr;
  struct PostInfo {
    int post_id;
    std::string account;
    std::string username;
    std::string title;
    std::string summary;
    std::string grade;
    std::string major;
    std::string role;
    std::string category;
    int is_anonymous;
    int view_count;
    int like_count;
    bool is_top;
    int comment_count;
    std::string created_at;
  };
  std::vector<PostInfo> posts;

  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *posts = static_cast<std::vector<PostInfo> *>(data);
        PostInfo post;
        post.post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post.account = argv[1] ? argv[1] : "";
        post.username = argv[2] ? argv[2] : "";
        post.title = argv[3] ? argv[3] : "";
        post.summary = argv[4] ? argv[4] : "";
        post.grade = argv[5] ? argv[5] : "";
        post.major = argv[6] ? argv[6] : "";
        post.role = argv[7] ? argv[7] : "";
        post.category = argv[8] ? argv[8] : "";
        post.is_anonymous = argv[9] ? std::stoi(argv[9]) : 0;
        post.view_count = argv[10] ? std::stoi(argv[10]) : 0;
        post.like_count = argv[11] ? std::stoi(argv[11]) : 0;
        post.is_top = argv[12] ? std::string(argv[12]) == "1" : false;
        post.comment_count = argv[13] ? std::stoi(argv[13]) : 0;
        post.created_at = argv[14] ? argv[14] : "";
        posts->push_back(post);
        return 0;
      },
      &posts, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "get_posts_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve posts";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json posts_json = nlohmann::json::array();
    for (const auto &post : posts) {
      std::string display_username =
          post.is_anonymous ? "匿名用户" : post.username;
      nlohmann::json item = {{"post_id", post.post_id},
                             {"username", display_username},
                             {"title", post.title},
                             {"summary", post.summary},
                             {"grade", post.grade},
                             {"major", post.major},
                             {"role", post.role},
                             {"category", post.category},
                             {"is_anonymous", post.is_anonymous},
                             {"view_count", post.view_count},
                             {"like_count", post.like_count},
                             {"is_top", post.is_top},
                             {"comment_count", post.comment_count},
                             {"created_at", post.created_at}};
      if (!post.is_anonymous) {
        item["account"] = post.account;
      }
      posts_json.push_back(item);
    }
    response_json["data"] = posts_json;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/posts/id - 获取单个帖子详情，浏览量增加
void handleGetPostDetail(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  nlohmann::json response_json;

  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  char *err_msg = nullptr;
  // 先更新浏览量
  std::string update_sql =
      "UPDATE posts SET view_count=view_count+1 WHERE post_id=" +
      std::to_string(post_id) + " AND is_deleted=0;";
  int rc = sqlite3_exec(db, update_sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "update_view_count_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to update view count";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (sqlite3_changes(db) == 0) {
    response_json["status"] = "failure";
    response_json["message"] = "Post not found or has been deleted";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  std::string sql =
      "SELECT p.post_id, p.account, u.username, p.title, p.content, "
      "u.grade, u.major, u.role, p.category, p.is_anonymous, "
      "p.view_count, p.like_count, p.is_top, p.comment_count, "
      "p.created_at, p.updated_at "
      "FROM posts p "
      "LEFT JOIN users u ON p.account = u.account "
      "WHERE p.post_id=" +
      std::to_string(post_id) + " AND p.is_deleted=0;";
  struct PostInfo {
    int post_id;
    std::string account;
    std::string username;
    std::string title;
    std::string content;
    std::string grade;
    std::string major;
    std::string role;
    std::string category;
    int is_anonymous;
    int view_count;
    int like_count;
    bool is_top;
    int comment_count;
    std::string created_at;
    std::string updated_at;
  };
  PostInfo post;
  bool post_found = false;
  auto post_data_pair = std::make_pair(&post, &post_found);
  rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *post_data = static_cast<std::pair<PostInfo *, bool *> *>(data);
        PostInfo *post = post_data->first;
        *(post_data->second) = true;
        post->post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post->account = argv[1] ? argv[1] : "";
        post->username = argv[2] ? argv[2] : "";
        post->title = argv[3] ? argv[3] : "";
        post->content = argv[4] ? argv[4] : "";
        post->grade = argv[5] ? argv[5] : "";
        post->major = argv[6] ? argv[6] : "";
        post->role = argv[7] ? argv[7] : "";
        post->category = argv[8] ? argv[8] : "";
        post->is_anonymous = argv[9] ? std::stoi(argv[9]) : 0;
        post->view_count = argv[10] ? std::stoi(argv[10]) : 0;
        post->like_count = argv[11] ? std::stoi(argv[11]) : 0;
        post->is_top = argv[12] ? std::string(argv[12]) == "1" : false;
        post->comment_count = argv[13] ? std::stoi(argv[13]) : 0;
        post->created_at = argv[14] ? argv[14] : "";
        post->updated_at = argv[15] ? argv[15] : "";
        return 0;
      },
      &post_data_pair, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "get_post_detail_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve post details";
    res.status = 500;
  } else if (!post_found) {
    response_json["status"] = "failure";
    response_json["message"] = "Post not found";
    res.status = 404;
  } else {
    std::string display_username =
        post.is_anonymous ? "匿名用户" : post.username;
    nlohmann::json item = {{"post_id", post.post_id},
                           {"username", display_username},
                           {"title", post.title},
                           {"content", post.content},
                           {"grade", post.grade},
                           {"major", post.major},
                           {"role", post.role},
                           {"category", post.category},
                           {"is_anonymous", post.is_anonymous},
                           {"view_count", post.view_count},
                           {"like_count", post.like_count},
                           {"is_top", post.is_top},
                           {"comment_count", post.comment_count},
                           {"created_at", post.created_at},
                           {"updated_at", post.updated_at}};
    if (!post.is_anonymous) {
      item["account"] = post.account;
    }
    response_json["status"] = "success";
    response_json["data"] = item;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/posts - 创建新帖子
void handleCreatePost(const httplib::Request &req, httplib::Response &res) {
  // json:{"title":"xxx","content":"xxx","category":"xxx","is_anonymous":0}
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  std::string title = body["title"];
  std::string content = body["content"];
  std::string category = body.value("category", "");
  int is_anonymous = body.value("is_anonymous", 0);

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string sql =
      "INSERT INTO posts (account, title, content, category, is_anonymous, "
      "view_count, like_count, is_top, comment_count, is_deleted, created_at, "
      "updated_at) VALUES ('" +
      cookie_account + "', '" + title + "', '" + content + "', " +
      (category.empty() ? "NULL" : ("'" + category + "'")) + ", " +
      std::to_string(is_anonymous) +
      ", 0, 0, 0, 0, 0, datetime('now'), datetime('now'));";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "create_post_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to create post";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] = "Post created successfully";
    res.status = 201;
  }
  res.set_content(response_json.dump(), "application/json");
}
// PUT /api/posts/id - 更新帖子内容标题时间
void handleUpdatePost(const httplib::Request &req, httplib::Response &res) {
  // json:{"post_id":xxx,"title":"xxx","content":"xxx"}
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  std::string title = body["title"];
  std::string content = body["content"];
  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 只允许本人修改自己的帖子
  std::string sql =
      "UPDATE posts SET title='" + title + "', content='" + content +
      "', updated_at=datetime('now') WHERE post_id=" + std::to_string(post_id) +
      " AND account='" + cookie_account + "' AND is_deleted=0;";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "update_post_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to update post";
    res.status = 500;
  } else if (sqlite3_changes(db) == 0) {
    response_json["status"] = "failure";
    response_json["message"] = "Post not found or not owned by user";
    res.status = 404;
  } else {
    response_json["status"] = "success";
    response_json["message"] = "Post updated successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// DELETE /api/posts/id - 软删除帖子
void handleDeletePost(const httplib::Request &req, httplib::Response &res) {
  // json:{"post_id":xxx}
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 只允许本人软删除自己的帖子
  std::string sql =
      "UPDATE posts SET is_deleted=1 WHERE post_id=" + std::to_string(post_id) +
      " AND account='" + cookie_account + "';";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "delete_post_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to delete post";
    res.status = 500;
  } else if (sqlite3_changes(db) == 0) {
    response_json["status"] = "failure";
    response_json["message"] = "Post not found or not owned by user";
    res.status = 404;
  } else {
    response_json["status"] = "success";
    response_json["message"] = "Post deleted successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
//  POST /api/posts/hot - 获取热门帖子
void handleGetHotPosts(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 分页参数，支持 json body 或 url 参数
  int page = 1, page_size = 10;
  if (!req.body.empty()) {
    auto body = nlohmann::json::parse(req.body);
    page = body.value("page", 1);
    page_size = body.value("page_size", 10);
  }
  if (req.has_param("page"))
    page = std::stoi(req.get_param_value("page"));
  if (req.has_param("page_size"))
    page_size = std::stoi(req.get_param_value("page_size"));
  int offset = (page - 1) * page_size;

  // 添加分页功能
  std::string sql =
      "SELECT p.post_id, p.account, u.username, p.title, "
      "SUBSTR(p.content, 1, 100) as summary, "
      "u.grade, u.major, u.role, p.category, p.is_anonymous, "
      "p.view_count, p.like_count, p.is_top, p.comment_count, p.created_at "
      "FROM posts p "
      "LEFT JOIN users u ON p.account = u.account "
      "WHERE p.is_deleted=0 "
      "ORDER BY p.view_count DESC, p.like_count DESC "
      "LIMIT " +
      std::to_string(page_size) + " OFFSET " + std::to_string(offset) +
      ";"; // 分页

  char *err_msg = nullptr;
  struct PostInfo {
    int post_id;
    std::string account;
    std::string username;
    std::string title;
    std::string summary;
    std::string grade;
    std::string major;
    std::string role;
    std::string category;
    int is_anonymous;
    int view_count;
    int like_count;
    bool is_top;
    int comment_count;
    std::string created_at;
  };
  std::vector<PostInfo> posts;

  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *posts = static_cast<std::vector<PostInfo> *>(data);
        PostInfo post;
        post.post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post.account = argv[1] ? argv[1] : "";
        post.username = argv[2] ? argv[2] : "";
        post.title = argv[3] ? argv[3] : "";
        post.summary = argv[4] ? argv[4] : "";
        post.grade = argv[5] ? argv[5] : "";
        post.major = argv[6] ? argv[6] : "";
        post.role = argv[7] ? argv[7] : "";
        post.category = argv[8] ? argv[8] : "";
        post.is_anonymous = argv[9] ? std::stoi(argv[9]) : 0;
        post.view_count = argv[10] ? std::stoi(argv[10]) : 0;
        post.like_count = argv[11] ? std::stoi(argv[11]) : 0;
        post.is_top = argv[12] ? std::string(argv[12]) == "1" : false;
        post.comment_count = argv[13] ? std::stoi(argv[13]) : 0;
        post.created_at = argv[14] ? argv[14] : "";
        posts->push_back(post);
        return 0;
      },
      &posts, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "get_hot_posts_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve hot posts";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json posts_json = nlohmann::json::array();
    for (const auto &post : posts) {
      std::string display_username =
          post.is_anonymous ? "匿名用户" : post.username;
      nlohmann::json item = {{"post_id", post.post_id},
                             {"username", display_username},
                             {"title", post.title},
                             {"summary", post.summary},
                             {"grade", post.grade},
                             {"major", post.major},
                             {"role", post.role},
                             {"category", post.category},
                             {"is_anonymous", post.is_anonymous},
                             {"view_count", post.view_count},
                             {"like_count", post.like_count},
                             {"is_top", post.is_top},
                             {"comment_count", post.comment_count},
                             {"created_at", post.created_at}};
      if (!post.is_anonymous) {
        item["account"] = post.account;
      }
      posts_json.push_back(item);
    }
    response_json["data"] = posts_json;
    response_json["page"] = page;           // 当前页码
    response_json["page_size"] = page_size; // 每页数量
    response_json["count"] = posts.size();  // 当前页数量
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/posts/pinned - 获取置顶帖子
void handleGetPinnedPosts(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 分页参数，支持 json body 或 url 参数
  int page = 1, page_size = 10;
  if (!req.body.empty()) {
    auto body = nlohmann::json::parse(req.body);
    page = body.value("page", 1);
    page_size = body.value("page_size", 10);
  }
  if (req.has_param("page"))
    page = std::stoi(req.get_param_value("page"));
  if (req.has_param("page_size"))
    page_size = std::stoi(req.get_param_value("page_size"));
  int offset = (page - 1) * page_size;

  // 添加分页功能
  std::string sql =
      "SELECT p.post_id, p.account, u.username, p.title, SUBSTR(p.content, 1, "
      "100) as summary, "
      "u.grade, u.major, u.role, p.category, p.is_anonymous, "
      "p.view_count, p.like_count, p.is_top, p.comment_count, p.created_at "
      "FROM posts p "
      "LEFT JOIN users u ON p.account = u.account "
      "WHERE p.is_deleted=0 AND p.is_top=1 "
      "ORDER BY p.created_at DESC "
      "LIMIT " +
      std::to_string(page_size) + " OFFSET " + std::to_string(offset) + ";";

  char *err_msg = nullptr;
  struct PostInfo {
    int post_id;
    std::string account;
    std::string username;
    std::string title;
    std::string summary;
    std::string grade;
    std::string major;
    std::string role;
    std::string category;
    int is_anonymous;
    int view_count;
    int like_count;
    bool is_top;
    int comment_count;
    std::string created_at;
  };
  std::vector<PostInfo> posts;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *posts = static_cast<std::vector<PostInfo> *>(data);
        PostInfo post;
        post.post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post.account = argv[1] ? argv[1] : "";
        post.username = argv[2] ? argv[2] : "";
        post.title = argv[3] ? argv[3] : "";
        post.summary = argv[4] ? argv[4] : "";
        post.grade = argv[5] ? argv[5] : "";
        post.major = argv[6] ? argv[6] : "";
        post.role = argv[7] ? argv[7] : "";
        post.category = argv[8] ? argv[8] : "";
        post.is_anonymous = argv[9] ? std::stoi(argv[9]) : 0;
        post.view_count = argv[10] ? std::stoi(argv[10]) : 0;
        post.like_count = argv[11] ? std::stoi(argv[11]) : 0;
        post.is_top = argv[12] ? std::string(argv[12]) == "1" : false;
        post.comment_count = argv[13] ? std::stoi(argv[13]) : 0;
        post.created_at = argv[14] ? argv[14] : "";
        posts->push_back(post);
        return 0;
      },
      &posts, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "get_pinned_posts_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve pinned posts";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json posts_json = nlohmann::json::array();
    for (const auto &post : posts) {
      std::string display_username =
          post.is_anonymous ? "匿名用户" : post.username;
      nlohmann::json item = {{"post_id", post.post_id},
                             {"username", display_username},
                             {"title", post.title},
                             {"summary", post.summary},
                             {"grade", post.grade},
                             {"major", post.major},
                             {"role", post.role},
                             {"category", post.category},
                             {"is_anonymous", post.is_anonymous},
                             {"view_count", post.view_count},
                             {"like_count", post.like_count},
                             {"is_top", post.is_top},
                             {"comment_count", post.comment_count},
                             {"created_at", post.created_at}};
      if (!post.is_anonymous) {
        item["account"] = post.account;
      }
      posts_json.push_back(item);
    }
    response_json["data"] = posts_json;
    response_json["page"] = page;           // 当前页码
    response_json["page_size"] = page_size; // 每页数量
    response_json["count"] = posts.size();  // 当前页数量
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
//  POST /api/posts/id/like - 点赞/取消点赞帖子
void handleToggleLike(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  //检查用户是否已点赞该帖子
  std::string check_sql = "SELECT COUNT(*) FROM user_likes WHERE account='" +
                          cookie_account +
                          "' AND post_id=" + std::to_string(post_id) + ";";
  char *err_msg = nullptr;
  int like_count = 0;
  int rc = sqlite3_exec(
      db, check_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *like_count = static_cast<int *>(data);
        *like_count = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &like_count, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "toggle_like_check_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to toggle like";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (like_count > 0) {
    // 用户已点赞，执行取消点赞
    std::string delete_sql =
        "DELETE FROM user_likes WHERE account='" + cookie_account +
        "' AND post_id=" + std::to_string(post_id) +
        ";"
        "UPDATE posts SET like_count=like_count-1 WHERE post_id=" +
        std::to_string(post_id) + ";";
    rc = sqlite3_exec(db, delete_sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      std::cerr << "toggle_like_unlike_error: " << err_msg << std::endl;
      sqlite3_free(err_msg);
      response_json["status"] = "failure";
      response_json["message"] = "Failed to unlike post";
      res.status = 500;
    } else {
      response_json["status"] = "success";
      response_json["message"] = "Post unliked successfully";
      res.status = 200;
    }
  } else {
    // 用户未点赞，执行点赞
    std::string insert_sql =
        "INSERT INTO user_likes (account, post_id) VALUES ('" + cookie_account +
        "', " + std::to_string(post_id) +
        ");"
        "UPDATE posts SET like_count=like_count+1 WHERE post_id=" +
        std::to_string(post_id) + ";";
    rc = sqlite3_exec(db, insert_sql.c_str(), nullptr, nullptr, &err_msg);

    // 点赞成功后插入通知
    if (rc == SQLITE_OK) {
      // 查询帖子作者账号
      std::string info_sql =
          "SELECT account FROM posts WHERE post_id=" + std::to_string(post_id) +
          ";";
      std::string receiver_account;
      sqlite3_exec(
          db, info_sql.c_str(),
          [](void *data, int argc, char **argv, char **azColName) -> int {
            auto *receiver_account = static_cast<std::string *>(data);
            *receiver_account = argv[0] ? argv[0] : "";
            return 0;
          },
          &receiver_account, nullptr);

      if (!receiver_account.empty() && receiver_account != cookie_account) {
        std::string notify_sql =
            "INSERT INTO notifications (sender_account, receiver_account, "
            "type, related_id, content, is_read, created_at) VALUES ('" +
            cookie_account + "', '" + receiver_account + "', 'likepost', " +
            std::to_string(post_id) +
            ", '点赞了你的帖子', 0, datetime('now'));";
        sqlite3_exec(db, notify_sql.c_str(), nullptr, nullptr, nullptr);
      }
      response_json["status"] = "success";
      response_json["message"] = "Post liked successfully";
      res.status = 200;
    } else {
      std::cerr << "toggle_like_like_error: " << err_msg << std::endl;
      sqlite3_free(err_msg);
      response_json["status"] = "failure";
      response_json["message"] = "Failed to like post";
      res.status = 500;
    }
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/posts/id/favorite - 收藏/取消收藏帖子
void handleToggleFavorite(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  //检查用户是否已收藏该帖子
  std::string check_sql =
      "SELECT COUNT(*) FROM user_favorites WHERE account='" + cookie_account +
      "' AND post_id=" + std::to_string(post_id) + ";";
  char *err_msg = nullptr;
  int favorite_count = 0;
  int rc = sqlite3_exec(
      db, check_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *favorite_count = static_cast<int *>(data);
        *favorite_count = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &favorite_count, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "toggle_favorite_check_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to toggle favorite";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (favorite_count > 0) {
    // 用户已收藏，执行取消收藏
    std::string delete_sql = "DELETE FROM user_favorites WHERE account='" +
                             cookie_account +
                             "' AND post_id=" + std::to_string(post_id) + ";";
    rc = sqlite3_exec(db, delete_sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      std::cerr << "toggle_favorite_unfavorite_error: " << err_msg << std::endl;
      sqlite3_free(err_msg);
      response_json["status"] = "failure";
      response_json["message"] = "Failed to unfavorite post";
      res.status = 500;
    } else {
      response_json["status"] = "success";
      response_json["message"] = "Post unfavorited successfully";
      res.status = 200;
    }
  } else {
    // 用户未收藏，执行收藏
    std::string insert_sql =
        "INSERT INTO user_favorites (account, post_id) VALUES ('" +
        cookie_account + "', " + std::to_string(post_id) + ");";
    rc = sqlite3_exec(db, insert_sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      std::cerr << "toggle_favorite_favorite_error: " << err_msg << std::endl;
      sqlite3_free(err_msg);
      response_json["status"] = "failure";
      response_json["message"] = "Failed to favorite post";
      res.status = 500;
    } else {
      response_json["status"] = "success";
      response_json["message"] = "Post favorited successfully";
      res.status = 200;
    }
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/posts/search?q=关键词 - 搜索帖子
void handleSearchPosts(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  std::string keyword = req.get_param_value("q");
  int page = req.has_param("page") ? std::stoi(req.get_param_value("page")) : 1;
  int page_size = req.has_param("page_size")
                      ? std::stoi(req.get_param_value("page_size"))
                      : 10;
  int offset = (page - 1) * page_size;
  nlohmann::json response_json;
  if (keyword.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "Search keyword is required";
    res.status = 400;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string sql =
      "SELECT p.post_id, p.account, u.username, p.title, SUBSTR(p.content, 1, "
      "100) as summary, "
      "u.grade, u.major, u.role, p.category, p.is_anonymous, "
      "p.view_count, p.like_count, p.is_top, p.comment_count, p.created_at "
      "FROM posts p "
      "LEFT JOIN users u ON p.account = u.account "
      "WHERE p.is_deleted=0 AND ("
      "p.title LIKE '%" +
      keyword +
      "%' COLLATE NOCASE OR "
      "p.content LIKE '%" +
      keyword +
      "%' COLLATE NOCASE"
      ") "
      "ORDER BY p.is_top DESC, p.created_at DESC "
      "LIMIT " +
      std::to_string(page_size) + " OFFSET " + std::to_string(offset) + ";";
  char *err_msg = nullptr;
  struct PostInfo {
    int post_id;
    std::string account;
    std::string username;
    std::string title;
    std::string summary;
    std::string grade;
    std::string major;
    std::string role;
    std::string category;
    int is_anonymous;
    int view_count;
    int like_count;
    bool is_top;
    int comment_count;
    std::string created_at;
  };
  std::vector<PostInfo> posts;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *posts = static_cast<std::vector<PostInfo> *>(data);
        PostInfo post;
        post.post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post.account = argv[1] ? argv[1] : "";
        post.username = argv[2] ? argv[2] : "";
        post.title = argv[3] ? argv[3] : "";
        post.summary = argv[4] ? argv[4] : "";
        post.grade = argv[5] ? argv[5] : "";
        post.major = argv[6] ? argv[6] : "";
        post.role = argv[7] ? argv[7] : "";
        post.category = argv[8] ? argv[8] : "";
        post.is_anonymous = argv[9] ? std::stoi(argv[9]) : 0;
        post.view_count = argv[10] ? std::stoi(argv[10]) : 0;
        post.like_count = argv[11] ? std::stoi(argv[11]) : 0;
        post.is_top = argv[12] ? std::string(argv[12]) == "1" : false;
        post.comment_count = argv[13] ? std::stoi(argv[13]) : 0;
        post.created_at = argv[14] ? argv[14] : "";
        posts->push_back(post);
        return 0;
      },
      &posts, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "search_posts_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to search posts";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["data"] = nlohmann::json::array();
    for (const auto &post : posts) {
      std::string display_username =
          post.is_anonymous ? "匿名用户" : post.username;
      nlohmann::json item = {{"post_id", post.post_id},
                             {"username", display_username},
                             {"title", post.title},
                             {"summary", post.summary},
                             {"grade", post.grade},
                             {"major", post.major},
                             {"role", post.role},
                             {"category", post.category},
                             {"is_anonymous", post.is_anonymous},
                             {"view_count", post.view_count},
                             {"like_count", post.like_count},
                             {"is_top", post.is_top},
                             {"comment_count", post.comment_count},
                             {"created_at", post.created_at}};
      if (!post.is_anonymous) {
        item["account"] = post.account;
      }
      response_json["data"].push_back(item);
    }
    response_json["total_results"] = posts.size();
    response_json["keyword"] = keyword;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
} // namespace PostAPI
//评论接口
namespace CommentAPI {
// POST/api/comments/parent 获取帖子的父评论,时间排序
void handleGetParentComments(const httplib::Request &req,
                             httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  int page = body.value("page", 1);
  int page_size = body.value("page_size", 10);
  int offset = (page - 1) * page_size;

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string sql =
      "SELECT c.comment_id, c.account, u.username, c.content, "
      "COALESCE(c.parent_id, 0) as parent_id, "
      "c.like_count, c.created_at, c.is_anonymous "
      "FROM comments c "
      "LEFT JOIN users u ON c.account = u.account "
      "WHERE c.post_id=" +
      std::to_string(post_id) +
      " AND c.is_deleted=0 AND (c.parent_id IS NULL OR c.parent_id=0) "
      "ORDER BY c.created_at DESC "
      "LIMIT " +
      std::to_string(page_size) + " OFFSET " + std::to_string(offset) + ";";
  char *err_msg = nullptr;
  struct CommentInfo {
    int comment_id;
    std::string account;
    std::string username;
    std::string content;
    int parent_id;
    int like_count;
    std::string created_at;
    int is_anonymous;
  };
  std::vector<CommentInfo> comments;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *comments = static_cast<std::vector<CommentInfo> *>(data);
        CommentInfo comment;
        comment.comment_id = argv[0] ? std::stoi(argv[0]) : 0;
        comment.account = argv[1] ? argv[1] : "";
        comment.username = argv[2] ? argv[2] : "";
        comment.content = argv[3] ? argv[3] : "";
        comment.parent_id = argv[4] ? std::stoi(argv[4]) : 0;
        comment.like_count = argv[5] ? std::stoi(argv[5]) : 0;
        comment.created_at = argv[6] ? argv[6] : "";
        comment.is_anonymous = argv[7] ? std::stoi(argv[7]) : 0;
        comments->push_back(comment);
        return 0;
      },
      &comments, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "get_parent_comments_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve parent comments";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json comments_json = nlohmann::json::array();
    for (const auto &comment : comments) {
      std::string display_username =
          comment.is_anonymous ? "匿名用户" : comment.username;
      nlohmann::json item = {{"comment_id", comment.comment_id},
                             {"username", display_username},
                             {"content", comment.content},
                             {"parent_id", comment.parent_id},
                             {"like_count", comment.like_count},
                             {"created_at", comment.created_at}};
      if (!comment.is_anonymous) {
        item["account"] = comment.account;
      }
      comments_json.push_back(item);
    }
    response_json["data"] = comments_json;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/comments/child 获取父评论的所有子评论，时间排序
void handleGetChildComments(const httplib::Request &req,
                            httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  int parent_id = body["parent_id"];
  int page = body.value("page", 1);
  int page_size = body.value("page_size", 10);
  int offset = (page - 1) * page_size;

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string sql = "SELECT c.comment_id, c.account, u.username, c.content, "
                    "COALESCE(c.parent_id, 0) as parent_id, "
                    "c.like_count, c.created_at, c.is_anonymous "
                    "FROM comments c "
                    "LEFT JOIN users u ON c.account = u.account "
                    "WHERE c.parent_id=" +
                    std::to_string(parent_id) +
                    " AND c.is_deleted=0 "
                    "ORDER BY c.created_at DESC "
                    "LIMIT " +
                    std::to_string(page_size) + " OFFSET " +
                    std::to_string(offset) + ";";
  char *err_msg = nullptr;
  struct CommentInfo {
    int comment_id;
    std::string account;
    std::string username;
    std::string content;
    int parent_id;
    int like_count;
    std::string created_at;
    int is_anonymous;
  };
  std::vector<CommentInfo> comments;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *comments = static_cast<std::vector<CommentInfo> *>(data);
        CommentInfo comment;
        comment.comment_id = argv[0] ? std::stoi(argv[0]) : 0;
        comment.account = argv[1] ? argv[1] : "";
        comment.username = argv[2] ? argv[2] : "";
        comment.content = argv[3] ? argv[3] : "";
        comment.parent_id = argv[4] ? std::stoi(argv[4]) : 0;
        comment.like_count = argv[5] ? std::stoi(argv[5]) : 0;
        comment.created_at = argv[6] ? argv[6] : "";
        comment.is_anonymous = argv[7] ? std::stoi(argv[7]) : 0;
        comments->push_back(comment);
        return 0;
      },
      &comments, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "get_child_comments_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve child comments";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json comments_json = nlohmann::json::array();
    for (const auto &comment : comments) {
      std::string display_username =
          comment.is_anonymous ? "匿名用户" : comment.username;
      nlohmann::json item = {{"comment_id", comment.comment_id},
                             {"username", display_username},
                             {"content", comment.content},
                             {"parent_id", comment.parent_id},
                             {"like_count", comment.like_count},
                             {"created_at", comment.created_at}};
      if (!comment.is_anonymous) {
        item["account"] = comment.account;
      }
      comments_json.push_back(item);
    }
    response_json["data"] = comments_json;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/comments/parent/likes 获取帖子的父评论（点赞数排序）
void handleGetParentCommentsByLikes(const httplib::Request &req,
                                    httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  int page = body.value("page", 1);
  int page_size = body.value("page_size", 10);
  int offset = (page - 1) * page_size;

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string sql =
      "SELECT c.comment_id, c.account, u.username, c.content, "
      "COALESCE(c.parent_id, 0) as parent_id, "
      "c.like_count, c.created_at, c.is_anonymous "
      "FROM comments c "
      "LEFT JOIN users u ON c.account = u.account "
      "WHERE c.post_id=" +
      std::to_string(post_id) +
      " AND c.is_deleted=0 AND (c.parent_id IS NULL OR c.parent_id=0) "
      "ORDER BY c.like_count DESC, c.created_at DESC "
      "LIMIT " +
      std::to_string(page_size) + " OFFSET " + std::to_string(offset) + ";";
  char *err_msg = nullptr;
  struct CommentInfo {
    int comment_id;
    std::string account;
    std::string username;
    std::string content;
    int parent_id;
    int like_count;
    std::string created_at;
    int is_anonymous;
  };
  std::vector<CommentInfo> comments;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *comments = static_cast<std::vector<CommentInfo> *>(data);
        CommentInfo comment;
        comment.comment_id = argv[0] ? std::stoi(argv[0]) : 0;
        comment.account = argv[1] ? argv[1] : "";
        comment.username = argv[2] ? argv[2] : "";
        comment.content = argv[3] ? argv[3] : "";
        comment.parent_id = argv[4] ? std::stoi(argv[4]) : 0;
        comment.like_count = argv[5] ? std::stoi(argv[5]) : 0;
        comment.created_at = argv[6] ? argv[6] : "";
        comment.is_anonymous = argv[7] ? std::stoi(argv[7]) : 0;
        comments->push_back(comment);
        return 0;
      },
      &comments, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "get_parent_comments_by_likes_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve parent comments";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json comments_json = nlohmann::json::array();
    for (const auto &comment : comments) {
      std::string display_username =
          comment.is_anonymous ? "匿名用户" : comment.username;
      nlohmann::json item = {{"comment_id", comment.comment_id},
                             {"username", display_username},
                             {"content", comment.content},
                             {"parent_id", comment.parent_id},
                             {"like_count", comment.like_count},
                             {"created_at", comment.created_at}};
      if (!comment.is_anonymous) {
        item["account"] = comment.account;
      }
      comments_json.push_back(item);
    }
    response_json["data"] = comments_json;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/comments/child/likes 获取父评论的所有子评论（点赞数排序）
void handleGetChildCommentsByLikes(const httplib::Request &req,
                                   httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  int parent_id = body["parent_id"];
  int page = body.value("page", 1);
  int page_size = body.value("page_size", 10);
  int offset = (page - 1) * page_size;

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string sql = "SELECT c.comment_id, c.account, u.username, c.content, "
                    "COALESCE(c.parent_id, 0) as parent_id, "
                    "c.like_count, c.created_at, c.is_anonymous "
                    "FROM comments c "
                    "LEFT JOIN users u ON c.account = u.account "
                    "WHERE c.parent_id=" +
                    std::to_string(parent_id) +
                    " AND c.is_deleted=0 "
                    "ORDER BY c.like_count DESC, c.created_at DESC "
                    "LIMIT " +
                    std::to_string(page_size) + " OFFSET " +
                    std::to_string(offset) + ";";
  char *err_msg = nullptr;
  struct CommentInfo {
    int comment_id;
    std::string account;
    std::string username;
    std::string content;
    int parent_id;
    int like_count;
    std::string created_at;
    int is_anonymous;
  };
  std::vector<CommentInfo> comments;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *comments = static_cast<std::vector<CommentInfo> *>(data);
        CommentInfo comment;
        comment.comment_id = argv[0] ? std::stoi(argv[0]) : 0;
        comment.account = argv[1] ? argv[1] : "";
        comment.username = argv[2] ? argv[2] : "";
        comment.content = argv[3] ? argv[3] : "";
        comment.parent_id = argv[4] ? std::stoi(argv[4]) : 0;
        comment.like_count = argv[5] ? std::stoi(argv[5]) : 0;
        comment.created_at = argv[6] ? argv[6] : "";
        comment.is_anonymous = argv[7] ? std::stoi(argv[7]) : 0;
        comments->push_back(comment);
        return 0;
      },
      &comments, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "get_child_comments_by_likes_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve child comments";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json comments_json = nlohmann::json::array();
    for (const auto &comment : comments) {
      std::string display_username =
          comment.is_anonymous ? "匿名用户" : comment.username;
      nlohmann::json item = {{"comment_id", comment.comment_id},
                             {"username", display_username},
                             {"content", comment.content},
                             {"parent_id", comment.parent_id},
                             {"like_count", comment.like_count},
                             {"created_at", comment.created_at}};
      if (!comment.is_anonymous) {
        item["account"] = comment.account;
      }
      comments_json.push_back(item);
    }
    response_json["data"] = comments_json;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/posts/post_id/comments - 发表评论
void handleCreateComment(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  std::string content = body["content"];
  int parent_id = body.value("parent_id", 0);
  int is_anonymous = body.value("is_anonymous", 0);

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 新增校验：不允许回复父评论不是0或空的评论
  if (parent_id != 0) {
    std::string parent_sql =
        "SELECT parent_id FROM comments WHERE comment_id=" +
        std::to_string(parent_id) + ";";
    int parent_parent_id = 0;
    char *parent_err = nullptr;
    int parent_rc = sqlite3_exec(
        db, parent_sql.c_str(),
        [](void *data, int argc, char **argv, char **azColName) -> int {
          int *pid = static_cast<int *>(data);
          *pid = argv[0] ? std::stoi(argv[0]) : 0;
          return 0;
        },
        &parent_parent_id, &parent_err);
    if (parent_rc != SQLITE_OK) {
      if (parent_err)
        std::cerr << "parent_comment_error: " << parent_err << std::endl;
      if (parent_err)
        sqlite3_free(parent_err);
      response_json["status"] = "failure";
      response_json["message"] = "Failed to check parent comment";
      res.status = 500;
      res.set_content(response_json.dump(), "application/json");
      return;
    }
    if (parent_parent_id != 0) {
      response_json["status"] = "failure";
      response_json["message"] = "只能回复父评论，不能回复子评论";
      res.status = 400;
      res.set_content(response_json.dump(), "application/json");
      return;
    }
  }

  std::string sql;
  if (parent_id == 0) {
    sql = "INSERT INTO comments (post_id, account, content, parent_id, "
          "like_count, is_deleted, is_anonymous, created_at) "
          "VALUES (" +
          std::to_string(post_id) + ", '" + cookie_account + "', '" + content +
          "', NULL, 0, 0, " + std::to_string(is_anonymous) +
          ", datetime('now'));";
  } else {
    sql = "INSERT INTO comments (post_id, account, content, parent_id, "
          "like_count, is_deleted, is_anonymous, created_at) "
          "VALUES (" +
          std::to_string(post_id) + ", '" + cookie_account + "', '" + content +
          "', " + std::to_string(parent_id) + ", 0, 0, " +
          std::to_string(is_anonymous) + ", datetime('now'));";
  }
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "create_comment_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to create comment";
    res.status = 500;
  } else {
    int new_comment_id = sqlite3_last_insert_rowid(db);
    std::string update_sql =
        "UPDATE posts SET comment_count=comment_count+1 WHERE post_id=" +
        std::to_string(post_id) + ";";
    sqlite3_exec(db, update_sql.c_str(), nullptr, nullptr, nullptr);

    if (parent_id == 0) {
      std::string post_author_sql =
          "SELECT account FROM posts WHERE post_id=" + std::to_string(post_id) +
          ";";
      std::string receiver_account;
      sqlite3_exec(
          db, post_author_sql.c_str(),
          [](void *data, int argc, char **argv, char **azColName) -> int {
            auto *receiver_account = static_cast<std::string *>(data);
            *receiver_account = argv[0] ? argv[0] : "";
            return 0;
          },
          &receiver_account, nullptr);
      if (!receiver_account.empty() && receiver_account != cookie_account) {
        std::string notify_sql =
            "INSERT INTO notifications (sender_account, receiver_account, "
            "type, related_id, content, is_read, created_at) VALUES ('" +
            cookie_account + "', '" + receiver_account + "', 'comment', " +
            std::to_string(new_comment_id) +
            ", '评论了你的帖子', 0, datetime('now'));";
        sqlite3_exec(db, notify_sql.c_str(), nullptr, nullptr, nullptr);
      }
    } else {
      std::string parent_author_sql =
          "SELECT account FROM comments WHERE comment_id=" +
          std::to_string(parent_id) + ";";
      std::string receiver_account;
      sqlite3_exec(
          db, parent_author_sql.c_str(),
          [](void *data, int argc, char **argv, char **azColName) -> int {
            auto *receiver_account = static_cast<std::string *>(data);
            *receiver_account = argv[0] ? argv[0] : "";
            return 0;
          },
          &receiver_account, nullptr);
      if (!receiver_account.empty() && receiver_account != cookie_account) {
        std::string notify_sql =
            "INSERT INTO notifications (sender_account, receiver_account, "
            "type, related_id, content, is_read, created_at) VALUES ('" +
            cookie_account + "', '" + receiver_account + "', 'reply', " +
            std::to_string(new_comment_id) +
            ", '回复了你的评论', 0, datetime('now'));";
        sqlite3_exec(db, notify_sql.c_str(), nullptr, nullptr, nullptr);
      }
    }

    response_json["status"] = "success";
    response_json["message"] = "Comment created successfully";
    res.status = 201;
  }
  res.set_content(response_json.dump(), "application/json");
}
// DELETE /api/comments/id - 删除评论
void handleDeleteComment(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  int comment_id = body["comment_id"];
  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 获取评论所属的post_id和账号
  std::string get_info_sql =
      "SELECT post_id, account FROM comments WHERE comment_id=" +
      std::to_string(comment_id) + ";";
  char *err_msg = nullptr;
  struct CommentInfo {
    int post_id = 0;
    std::string account;
  } comment_info;
  int rc = sqlite3_exec(
      db, get_info_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *info = static_cast<CommentInfo *>(data);
        info->post_id = argv[0] ? std::stoi(argv[0]) : 0;
        info->account = argv[1] ? argv[1] : "";
        return 0;
      },
      &comment_info, &err_msg);

  if (rc != SQLITE_OK || comment_info.post_id == 0) {
    std::cerr << "delete_comment_get_info_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to delete comment";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否本人
  if (comment_info.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "You can only delete your own comment";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  //软删除评论
  std::string sql = "UPDATE comments SET is_deleted=1 WHERE comment_id=" +
                    std::to_string(comment_id) + ";";
  rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "delete_comment_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to delete comment";
    res.status = 500;
  } else {
    // 更新帖子的评论数
    std::string update_sql =
        "UPDATE posts SET comment_count=comment_count-1 WHERE post_id=" +
        std::to_string(comment_info.post_id) + ";";
    sqlite3_exec(db, update_sql.c_str(), nullptr, nullptr, nullptr);
    response_json["status"] = "success";
    response_json["message"] = "Comment deleted successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/comments/id/like - 点赞/取消点赞评论
void handleToggleCommentLike(const httplib::Request &req,
                             httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  int comment_id = body["comment_id"];
  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  //检查用户是否已点赞该评论
  std::string check_sql =
      "SELECT COUNT(*) FROM comment_likes WHERE account='" + cookie_account +
      "' AND comment_id=" + std::to_string(comment_id) + ";";
  char *err_msg = nullptr;
  int like_count = 0;
  int rc = sqlite3_exec(
      db, check_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *like_count = static_cast<int *>(data);
        *like_count = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &like_count, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "toggle_comment_like_check_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to toggle comment like";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (like_count > 0) {
    // 用户已点赞，执行取消点赞
    std::string delete_sql =
        "DELETE FROM comment_likes WHERE account='" + cookie_account +
        "' AND comment_id=" + std::to_string(comment_id) +
        ";"
        "UPDATE comments SET like_count=like_count-1 WHERE comment_id=" +
        std::to_string(comment_id) + ";";
    rc = sqlite3_exec(db, delete_sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      std::cerr << "toggle_comment_like_unlike_error: " << err_msg << std::endl;
      sqlite3_free(err_msg);
      response_json["status"] = "failure";
      response_json["message"] = "Failed to unlike comment";
      res.status = 500;
    } else {
      response_json["status"] = "success";
      response_json["message"] = "Comment unliked successfully";
      res.status = 200;
    }
  } else {
    // 用户未点赞，执行点赞
    std::string insert_sql =
        "INSERT INTO comment_likes (account, comment_id) VALUES ('" +
        cookie_account + "', " + std::to_string(comment_id) +
        ");"
        "UPDATE comments SET like_count=like_count+1 WHERE comment_id=" +
        std::to_string(comment_id) + ";";
    rc = sqlite3_exec(db, insert_sql.c_str(), nullptr, nullptr, &err_msg);

    // 点赞成功后插入通知
    if (rc == SQLITE_OK) {
      // 查询评论作者账号
      std::string info_sql = "SELECT account FROM comments WHERE comment_id=" +
                             std::to_string(comment_id) + ";";
      std::string receiver_account;
      sqlite3_exec(
          db, info_sql.c_str(),
          [](void *data, int argc, char **argv, char **azColName) -> int {
            auto *receiver_account = static_cast<std::string *>(data);
            *receiver_account = argv[0] ? argv[0] : "";
            return 0;
          },
          &receiver_account, nullptr);

      if (!receiver_account.empty() && receiver_account != cookie_account) {
        std::string notify_sql =
            "INSERT INTO notifications (sender_account, receiver_account, "
            "type, related_id, content, is_read, created_at) VALUES ('" +
            cookie_account + "', '" + receiver_account + "', 'likecomment', " +
            std::to_string(comment_id) +
            ", '点赞了你的评论', 0, datetime('now'));";
        sqlite3_exec(db, notify_sql.c_str(), nullptr, nullptr, nullptr);
      }
      response_json["status"] = "success";
      response_json["message"] = "Comment liked successfully";
      res.status = 200;
    } else {
      std::cerr << "toggle_comment_like_like_error: " << err_msg << std::endl;
      sqlite3_free(err_msg);
      response_json["status"] = "failure";
      response_json["message"] = "Failed to like comment";
      res.status = 500;
    }
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/comments/detail - 获取评论详情
void handleGetCommentDetail(const httplib::Request &req,
                            httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  auto body = nlohmann::json::parse(req.body);
  int comment_id = body.value("comment_id", 0);
  nlohmann::json response_json;
  if (comment_id == 0) {
    response_json["status"] = "failure";
    response_json["message"] = "Comment ID is required";
    res.status = 400;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string sql = "SELECT c.comment_id, c.account, u.username, c.content, "
                    "COALESCE(c.parent_id, 0) as parent_id, "
                    "c.like_count, c.created_at, c.is_anonymous, c.post_id "
                    "FROM comments c "
                    "LEFT JOIN users u ON c.account = u.account "
                    "WHERE c.comment_id=" +
                    std::to_string(comment_id) + " AND c.is_deleted=0;";
  char *err_msg = nullptr;
  struct CommentDetail {
    int comment_id;
    std::string account;
    std::string username;
    std::string content;
    int parent_id;
    int like_count;
    std::string created_at;
    int is_anonymous;
    int post_id;
    bool found = false;
  };
  CommentDetail comment;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *comment = static_cast<CommentDetail *>(data);
        comment->comment_id = argv[0] ? std::stoi(argv[0]) : 0;
        comment->account = argv[1] ? argv[1] : "";
        comment->username = argv[2] ? argv[2] : "";
        comment->content = argv[3] ? argv[3] : "";
        comment->parent_id = argv[4] ? std::stoi(argv[4]) : 0;
        comment->like_count = argv[5] ? std::stoi(argv[5]) : 0;
        comment->created_at = argv[6] ? argv[6] : "";
        comment->is_anonymous = argv[7] ? std::stoi(argv[7]) : 0;
        comment->post_id = argv[8] ? std::stoi(argv[8]) : 0;
        comment->found = true;
        return 0;
      },
      &comment, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "get_comment_detail_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve comment detail";
    res.status = 500;
  } else if (!comment.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Comment not found";
    res.status = 404;
  } else {
    std::string display_username =
        comment.is_anonymous ? "匿名用户" : comment.username;
    nlohmann::json item = {{"comment_id", comment.comment_id},
                           {"post_id", comment.post_id},
                           {"username", display_username},
                           {"content", comment.content},
                           {"parent_id", comment.parent_id},
                           {"like_count", comment.like_count},
                           {"created_at", comment.created_at},
                           {"is_anonymous", comment.is_anonymous}};
    if (!comment.is_anonymous) {
      item["account"] = comment.account;
    }
    response_json["status"] = "success";
    response_json["data"] = item;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
} // namespace CommentAPI
//管理员接口
namespace AdminAPI {
// POST /api/admin/users - 创建新用户
void handleCreateUser(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // json:{"account":"xxx","username":"xxx","password":"xxx","major":"xxx","grade":"xxx","role":"xxx","is_admin":0}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string username = body["username"];
  std::string password = body["password"];
  std::string role = body.value("role", "student"); // 默认学生
  std::string major = body.value("major", "");
  std::string grade = body.value("grade", "");
  int is_admin = body.value("is_admin", 0);

  // 判断师生身份
  if (role == "teacher") {
    major = "";
    grade = "";
  } else if (role == "student") {
    if (major.empty() || grade.empty()) {
      response_json["status"] = "failure";
      response_json["message"] = "学生必须填写年级和专业";
      res.status = 400;
      res.set_content(response_json.dump(), "application/json");
      return;
    }
  }
  std::string sql = "INSERT INTO users (account, username, password, major, "
                    "grade, role, is_admin, is_online, is_banned) VALUES ('" +
                    account + "', '" + username + "', '" + password + "', " +
                    (major.empty() ? "NULL" : ("'" + major + "'")) + ", " +
                    (grade.empty() ? "NULL" : ("'" + grade + "'")) + ", " +
                    "'" + role + "', " + std::to_string(is_admin) + ", 0, 0);";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "create_user_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to create user";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] = "User created successfully";
    res.status = 201;
  }
  res.set_content(response_json.dump(), "application/json");
}
// PUT /api/admin/users/id - 修改用户是否为管理员
void handleUpdateUser(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // json:{"account":"xxx","is_admin":1}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  int is_admin = body.value("is_admin", 1); // 默认为1，支持0取消

  std::string sql = "UPDATE users SET is_admin=" + std::to_string(is_admin) +
                    " WHERE account='" + account + "';";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "update_user_admin_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to update admin status";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] = is_admin ? "User set as admin successfully"
                                        : "User unset as admin successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/admin/users/id - 获取用户所有信息
void handleGetUser(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string sql =
      "SELECT account, username, password, bio, major, grade, role, "
      "is_admin, is_online, is_banned, last_login_attempt, last_login_time "
      "FROM users WHERE account='" +
      account + "';";
  char *err_msg = nullptr;
  struct UserInfo {
    std::string account;
    std::string username;
    std::string password;
    std::string bio;
    std::string major;
    std::string grade;
    std::string role;
    bool is_admin;
    bool is_online;
    bool is_banned;
    int last_login_attempt;
    int last_login_time;
    bool found;
  };
  UserInfo user_info;
  user_info.found = false;
  // 获取用户信息
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *user_info = static_cast<UserInfo *>(data);
        user_info->account = argv[0] ? argv[0] : "";
        user_info->username = argv[1] ? argv[1] : "";
        user_info->password = argv[2] ? argv[2] : "";
        user_info->bio = argv[3] ? argv[3] : "";
        user_info->major = argv[4] ? argv[4] : "";
        user_info->grade = argv[5] ? argv[5] : "";
        user_info->role = argv[6] ? argv[6] : "";
        user_info->is_admin = argv[7] ? std::string(argv[7]) == "1" : false;
        user_info->is_online = argv[8] ? std::string(argv[8]) == "1" : false;
        user_info->is_banned = argv[9] ? std::string(argv[9]) == "1" : false;
        user_info->last_login_attempt = argv[10] ? std::stoi(argv[10]) : 0;
        user_info->last_login_time = argv[11] ? std::stoi(argv[11]) : 0;
        user_info->found = true;
        return 0;
      },
      &user_info, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "get_admin_user_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve user info";
    res.status = 500;
  } else if (!user_info.found) {
    response_json["status"] = "failure";
    response_json["message"] = "User not found";
    res.status = 404;
  } else {
    response_json["status"] = "success";
    response_json["data"] = {
        {"account", user_info.account},
        {"username", user_info.username},
        {"password", user_info.password},
        {"bio", user_info.bio},
        {"major", user_info.major},
        {"grade", user_info.grade},
        {"role", user_info.role},
        {"is_admin", user_info.is_admin},
        {"is_online", user_info.is_online},
        {"is_banned", user_info.is_banned},
        {"last_login_attempt", user_info.last_login_attempt},
        {"last_login_time", user_info.last_login_time}};
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// PUT /api/admin/users/id/password - 重置用户密码为123456
void handleResetUserPassword(const httplib::Request &req,
                             httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string new_password = "123456";
  std::string sql = "UPDATE users SET password='" + new_password +
                    "' WHERE account='" + account + "';";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "reset_user_password_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to reset user password";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] = "Password reset to 123456";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// DELETE /api/admin/users/id - 删除用户
void handleDeleteUser(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string sql = "DELETE FROM users WHERE account='" + account + "';";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "delete_user_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to delete user";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] = "User deleted successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/admin/users - 获取所有用户（account排序）
void handleGetAllUsers(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // json:{"page":1,"page_size":10}
  auto body = nlohmann::json::parse(req.body);
  int page = body.value("page", 1);
  int page_size = body.value("page_size", 10);
  int offset = (page - 1) * page_size;

  std::string sql =
      "SELECT account, username, bio, major, grade, role, is_admin, "
      "is_online, is_banned, last_login_attempt, last_login_time "
      "FROM users ORDER BY account ASC "
      "LIMIT " +
      std::to_string(page_size) + " OFFSET " + std::to_string(offset) + ";";
  char *err_msg = nullptr;
  struct UserInfo {
    std::string account;
    std::string username;
    std::string bio;
    std::string major;
    std::string grade;
    std::string role;
    bool is_admin;
    bool is_online;
    bool is_banned;
    int last_login_attempt;
    int last_login_time;
  };
  std::vector<UserInfo> users;
  // 获取用户列表
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *users = static_cast<std::vector<UserInfo> *>(data);
        UserInfo user;
        user.account = argv[0] ? argv[0] : "";
        user.username = argv[1] ? argv[1] : "";
        user.bio = argv[2] ? argv[2] : "";
        user.major = argv[3] ? argv[3] : "";
        user.grade = argv[4] ? argv[4] : "";
        user.role = argv[5] ? argv[5] : "";
        user.is_admin = argv[6] ? std::string(argv[6]) == "1" : false;
        user.is_online = argv[7] ? std::string(argv[7]) == "1" : false;
        user.is_banned = argv[8] ? std::string(argv[8]) == "1" : false;
        user.last_login_attempt = argv[9] ? std::stoi(argv[9]) : 0;
        user.last_login_time = argv[10] ? std::stoi(argv[10]) : 0;
        users->push_back(user);
        return 0;
      },
      &users, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "get_all_users_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve users";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json users_json = nlohmann::json::array();
    for (const auto &user : users) {
      users_json.push_back({{"account", user.account},
                            {"username", user.username},
                            {"bio", user.bio},
                            {"major", user.major},
                            {"grade", user.grade},
                            {"role", user.role},
                            {"is_admin", user.is_admin},
                            {"is_online", user.is_online},
                            {"is_banned", user.is_banned},
                            {"last_login_attempt", user.last_login_attempt},
                            {"last_login_time", user.last_login_time}});
    }
    response_json["data"] = users_json;
    response_json["page"] = page;
    response_json["page_size"] = page_size;
    response_json["count"] = users.size();
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// PUT /api/admin/users/id/ban - 封禁/解封用户
void handleToggleBanUser(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  //检查用户当前封禁状态
  std::string check_sql =
      "SELECT is_banned FROM users WHERE account='" + account + "';";
  char *err_msg = nullptr;
  int is_banned = 0;
  int rc = sqlite3_exec(
      db, check_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *is_banned = static_cast<int *>(data);
        *is_banned = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_banned, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "toggle_ban_user_check_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to toggle user ban status";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  //切换封禁状态
  int new_ban_status = is_banned ? 0 : 1;
  std::string update_sql =
      "UPDATE users SET is_banned=" + std::to_string(new_ban_status) +
      " WHERE account='" + account + "';";
  rc = sqlite3_exec(db, update_sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "toggle_ban_user_update_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to toggle user ban status";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] = new_ban_status ? "User banned successfully"
                                              : "User unbanned successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// DELETE /api/admin/posts/id - 硬删除帖子
void handleForceDeletePost(const httplib::Request &req,
                           httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string sql = "DELETE FROM posts WHERE is_deleted=1;";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "force_delete_post_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to force delete posts";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] =
        "All soft-deleted posts have been permanently deleted";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// DELETE /api/admin/comments/id - 硬删除评论
void handleForceDeleteComment(const httplib::Request &req,
                              httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string sql = "DELETE FROM comments WHERE is_deleted=1;";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "force_delete_comment_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to force delete comments";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] =
        "All soft-deleted comments have been permanently deleted";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
//  PUT /api/admin/posts/id/pin - 置顶/取消置顶帖子
void handleTogglePinPost(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // json:{"post_id":xxx}
  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];

  // 查询当前置顶状态
  std::string check_sql =
      "SELECT is_top FROM posts WHERE post_id=" + std::to_string(post_id) + ";";
  char *err_msg = nullptr;
  int is_top = 0;
  int rc = sqlite3_exec(
      db, check_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *is_top = static_cast<int *>(data);
        *is_top = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_top, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "toggle_pin_post_check_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to check pin status";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // 切换置顶状态
  int new_top_status = is_top ? 0 : 1;
  std::string update_sql =
      "UPDATE posts SET is_top=" + std::to_string(new_top_status) +
      " WHERE post_id=" + std::to_string(post_id) + ";";
  rc = sqlite3_exec(db, update_sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "toggle_pin_post_update_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to toggle pin status";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] = new_top_status ? "Post pinned" : "Post unpinned";
    response_json["is_top"] = new_top_status;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
//  GET /api/admin/stats - 获取平台统计数据，多少用户，多少帖子，多少评论
void handleGetStats(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  char *err_msg = nullptr;
  int user_count = 0, post_count = 0, comment_count = 0;

  // 查询用户总数
  std::string user_sql = "SELECT COUNT(*) FROM users;";
  int rc = sqlite3_exec(
      db, user_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *count = static_cast<int *>(data);
        *count = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &user_count, &err_msg);

  // 查询帖子总数
  std::string post_sql = "SELECT COUNT(*) FROM posts WHERE is_deleted=0;";
  rc |= sqlite3_exec(
      db, post_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *count = static_cast<int *>(data);
        *count = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &post_count, &err_msg);

  // 查询评论总数
  std::string comment_sql = "SELECT COUNT(*) FROM comments WHERE is_deleted=0;";
  rc |= sqlite3_exec(
      db, comment_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *count = static_cast<int *>(data);
        *count = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &comment_count, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "get_stats_error: " << (err_msg ? err_msg : "") << std::endl;
    if (err_msg)
      sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve stats";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["data"] = {{"user_count", user_count},
                             {"post_count", post_count},
                             {"comment_count", comment_count}};
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/admin/posts/deleted - 查看所有软删除的帖子
void handleGetDeletedPosts(const httplib::Request &req,
                           httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // json:{"page":1,"page_size":10}
  int page = 1, page_size = 10;
  if (!req.body.empty()) {
    auto body = nlohmann::json::parse(req.body);
    page = body.value("page", 1);
    page_size = body.value("page_size", 10);
  }
  int offset = (page - 1) * page_size;

  std::string sql =
      "SELECT p.post_id, p.account, u.username, p.title, SUBSTR(p.content, 1, "
      "100) as summary, "
      "u.grade, u.major, u.role, p.category, p.is_anonymous, "
      "p.view_count, p.like_count, p.is_top, p.comment_count, p.created_at "
      "FROM posts p "
      "LEFT JOIN users u ON p.account = u.account "
      "WHERE p.is_deleted=1 "
      "ORDER BY p.created_at DESC "
      "LIMIT " +
      std::to_string(page_size) + " OFFSET " + std::to_string(offset) + ";";

  char *err_msg = nullptr;
  struct PostInfo {
    int post_id;
    std::string account;
    std::string username;
    std::string title;
    std::string summary;
    std::string grade;
    std::string major;
    std::string role;
    std::string category;
    int is_anonymous;
    int view_count;
    int like_count;
    bool is_top;
    int comment_count;
    std::string created_at;
  };
  std::vector<PostInfo> posts;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *posts = static_cast<std::vector<PostInfo> *>(data);
        PostInfo post;
        post.post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post.account = argv[1] ? argv[1] : "";
        post.username = argv[2] ? argv[2] : "";
        post.title = argv[3] ? argv[3] : "";
        post.summary = argv[4] ? argv[4] : "";
        post.grade = argv[5] ? argv[5] : "";
        post.major = argv[6] ? argv[6] : "";
        post.role = argv[7] ? argv[7] : "";
        post.category = argv[8] ? argv[8] : "";
        post.is_anonymous = argv[9] ? std::stoi(argv[9]) : 0;
        post.view_count = argv[10] ? std::stoi(argv[10]) : 0;
        post.like_count = argv[11] ? std::stoi(argv[11]) : 0;
        post.is_top = argv[12] ? std::string(argv[12]) == "1" : false;
        post.comment_count = argv[13] ? std::stoi(argv[13]) : 0;
        post.created_at = argv[14] ? argv[14] : "";
        posts->push_back(post);
        return 0;
      },
      &posts, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "get_deleted_posts_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve deleted posts";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json posts_json = nlohmann::json::array();
    for (const auto &post : posts) {
      posts_json.push_back({{"post_id", post.post_id},
                            {"account", post.account},
                            {"username", post.username},
                            {"title", post.title},
                            {"summary", post.summary},
                            {"grade", post.grade},
                            {"major", post.major},
                            {"role", post.role},
                            {"category", post.category},
                            {"is_anonymous", post.is_anonymous},
                            {"view_count", post.view_count},
                            {"like_count", post.like_count},
                            {"is_top", post.is_top},
                            {"comment_count", post.comment_count},
                            {"created_at", post.created_at}});
    }
    response_json["data"] = posts_json;
    response_json["page"] = page;
    response_json["page_size"] = page_size;
    response_json["count"] = posts.size();
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/admin/comments/deleted - 查看所有软删除的评论
void handleGetDeletedComments(const httplib::Request &req,
                              httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // json:{"page":1,"page_size":10}
  int page = 1, page_size = 10;
  if (!req.body.empty()) {
    auto body = nlohmann::json::parse(req.body);
    page = body.value("page", 1);
    page_size = body.value("page_size", 10);
  }
  int offset = (page - 1) * page_size;

  std::string sql =
      "SELECT c.comment_id, c.post_id, c.account, u.username, c.content, "
      "COALESCE(c.parent_id, 0) as parent_id, "
      "c.like_count, c.created_at "
      "FROM comments c "
      "LEFT JOIN users u ON c.account = u.account "
      "WHERE c.is_deleted=1 "
      "ORDER BY c.created_at DESC "
      "LIMIT " +
      std::to_string(page_size) + " OFFSET " + std::to_string(offset) + ";";
  char *err_msg = nullptr;
  struct CommentInfo {
    int comment_id;
    int post_id;
    std::string account;
    std::string username;
    std::string content;
    int parent_id;
    int like_count;
    std::string created_at;
  };
  std::vector<CommentInfo> comments;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *comments = static_cast<std::vector<CommentInfo> *>(data);
        CommentInfo comment;
        comment.comment_id = argv[0] ? std::stoi(argv[0]) : 0;
        comment.post_id = argv[1] ? std::stoi(argv[1]) : 0;
        comment.account = argv[2] ? argv[2] : "";
        comment.username = argv[3] ? argv[3] : "";
        comment.content = argv[4] ? argv[4] : "";
        comment.parent_id = argv[5] ? std::stoi(argv[5]) : 0;
        comment.like_count = argv[6] ? std::stoi(argv[6]) : 0;
        comment.created_at = argv[7] ? argv[7] : "";
        comments->push_back(comment);
        return 0;
      },
      &comments, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "get_deleted_comments_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve deleted comments";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    nlohmann::json comments_json = nlohmann::json::array();
    for (const auto &comment : comments) {
      comments_json.push_back({{"comment_id", comment.comment_id},
                               {"post_id", comment.post_id},
                               {"account", comment.account},
                               {"username", comment.username},
                               {"content", comment.content},
                               {"parent_id", comment.parent_id},
                               {"like_count", comment.like_count},
                               {"created_at", comment.created_at}});
    }
    response_json["data"] = comments_json;
    response_json["page"] = page;
    response_json["page_size"] = page_size;
    response_json["count"] = comments.size();
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// PUT /api/admin/comments/id/recover - 恢复软删除评论
void handleRecoverComment(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // json:{"comment_id":xxx}
  auto body = nlohmann::json::parse(req.body);
  int comment_id = body["comment_id"];
  std::string sql = "UPDATE comments SET is_deleted=0 WHERE comment_id=" +
                    std::to_string(comment_id) + ";";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "recover_comment_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to recover comment";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] = "Comment recovered successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// PUT /api/admin/posts/id/recover - 恢复软删除帖子
void handleRecoverPost(const httplib::Request &req, httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // json:{"post_id":xxx}
  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  std::string sql =
      "UPDATE posts SET is_deleted=0 WHERE post_id=" + std::to_string(post_id) +
      ";";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  if (rc != SQLITE_OK) {
    std::cerr << "recover_post_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to recover post";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] = "Post recovered successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// PUT /api/admin/users/id/info - 管理员修改用户年级和专业
void handleUpdateUserMajorGrade(const httplib::Request &req,
                                httplib::Response &res) {
  // 从 cookie 获取 session_id 和 account
  std::string session_id, cookie_account;
  if (req.has_header("Cookie")) {
    std::string cookie = req.get_header_value("Cookie");
    size_t pos_id = cookie.find("session_id=");
    if (pos_id != std::string::npos) {
      size_t end_id = cookie.find(";", pos_id);
      session_id = cookie.substr(pos_id + 11, end_id == std::string::npos
                                                  ? std::string::npos
                                                  : end_id - pos_id - 11);
    }
    size_t pos_acc = cookie.find("account=");
    if (pos_acc != std::string::npos) {
      size_t end_acc = cookie.find(";", pos_acc);
      cookie_account = cookie.substr(pos_acc + 8, end_acc == std::string::npos
                                                      ? std::string::npos
                                                      : end_acc - pos_acc - 8);
    }
  }

  nlohmann::json response_json;
  if (session_id.empty() || cookie_account.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "No session_id or account found, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 校验 session_id 是否属于当前账号且未过期，并且是管理员
  std::string session_sql =
      "SELECT account, expires_at FROM sessions WHERE session_id='" +
      session_id + "';";
  struct SessionCheck {
    std::string account;
    int expires_at = 0;
    bool found = false;
  } session_check;
  char *session_err = nullptr;
  int session_rc = sqlite3_exec(
      db, session_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *session_check = static_cast<SessionCheck *>(data);
        session_check->account = argv[0] ? argv[0] : "";
        session_check->expires_at = argv[1] ? std::stoi(argv[1]) : 0;
        session_check->found = true;
        return 0;
      },
      &session_check, &session_err);

  int now = static_cast<int>(time(nullptr));
  if (session_rc != SQLITE_OK) {
    if (session_err)
      std::cerr << "session_error: " << session_err << std::endl;
    if (session_err)
      sqlite3_free(session_err);
    response_json["status"] = "failure";
    response_json["message"] = "Session query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!session_check.found) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not found";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.expires_at < now) {
    response_json["status"] = "failure";
    response_json["message"] = "Session expired, please login";
    res.status = 401;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (session_check.account != cookie_account) {
    response_json["status"] = "failure";
    response_json["message"] = "Session not match, please login";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 判断是否为管理员
  std::string admin_sql =
      "SELECT is_admin FROM users WHERE account='" + cookie_account + "';";
  int is_admin_flag = 0;
  char *admin_err = nullptr;
  int admin_rc = sqlite3_exec(
      db, admin_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        int *flag = static_cast<int *>(data);
        *flag = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &is_admin_flag, &admin_err);
  if (admin_rc != SQLITE_OK) {
    if (admin_err)
      std::cerr << "admin_error: " << admin_err << std::endl;
    if (admin_err)
      sqlite3_free(admin_err);
    response_json["status"] = "failure";
    response_json["message"] = "Admin query error";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  if (!is_admin_flag) {
    response_json["status"] = "failure";
    response_json["message"] = "No admin permission";
    res.status = 403;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  // json:{"account":"xxx","major":"xxx","grade":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string major = body.value("major", "");
  std::string grade = body.value("grade", "");
  std::string sql = "UPDATE users SET major='" + major + "', grade='" + grade +
                    "' WHERE account='" + account + "';";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "update_user_major_grade_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to update major and grade";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] = "Major and grade updated successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
} // namespace AdminAPI
int main() {
  // 初始化数据库
  int rc = sqlite3_open("forum.db", &db);
  if (rc) {
    std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
    return 1;
  }
  httplib::Server svr;
  // 用户接口
  svr.Post("/api/auth/login", UserAPI::handleLogin);
  svr.Post("/api/auth/logout", UserAPI::handleLogout);
  svr.Post("/api/users/search", UserAPI::handleSearchUsers);
  svr.Get("/api/users/id", UserAPI::handleGetUserInfo);
  svr.Put("/api/users/id", UserAPI::handleUpdateUserInfo);
  svr.Put("/api/users/id/password", UserAPI::handleChangePassword);
  svr.Get("/api/users/id/posts", UserAPI::handleGetUserPosts);
  svr.Get("/api/users/id/posts/count", UserAPI::handleGetUserPostCount);
  svr.Get("/api/users/id/comments", UserAPI::handleGetUserComments);
  svr.Get("/api/users/id/favorites", UserAPI::handleGetUserFavorites);
  svr.Get("/api/users/id/likes", UserAPI::handleGetUserLikes);
  svr.Get("/api/users/id/comment-likes", UserAPI::handleGetUserCommentLikes);
  svr.Post("/api/users/basic-info", UserAPI::handleGetUserBasicInfo);
  svr.Delete("/api/notifications/read",
             UserAPI::handleDeleteAllReadNotifications);
  svr.Delete("/api/notifications/id", UserAPI::handleDeleteNotification);

  // 帖子接口
  svr.Post("/api/posts", PostAPI::handleCreatePost);
  svr.Post("/api/handleGetPosts", PostAPI::handleGetPosts);
  svr.Post("/api/posts/handleGetPostDetail", PostAPI::handleGetPostDetail);
  svr.Put("/api/posts/id", PostAPI::handleUpdatePost);
  svr.Delete("/api/posts/id", PostAPI::handleDeletePost);
  svr.Post("/api/posts/hot", PostAPI::handleGetHotPosts);
  svr.Post("/api/posts/pinned", PostAPI::handleGetPinnedPosts);
  svr.Post("/api/posts/id/like", PostAPI::handleToggleLike);
  svr.Post("/api/posts/id/favorite", PostAPI::handleToggleFavorite);
  svr.Post("/api/posts/search", PostAPI::handleSearchPosts);

  // 评论接口
  svr.Post("/api/comments/parent", CommentAPI::handleGetParentComments);
  svr.Post("/api/comments/child", CommentAPI::handleGetChildComments);
  svr.Post("/api/comments/parent/likes",
           CommentAPI::handleGetParentCommentsByLikes);
  svr.Post("/api/comments/child/likes",
           CommentAPI::handleGetChildCommentsByLikes);
  svr.Post("/api/posts/post_id/comments", CommentAPI::handleCreateComment);
  svr.Delete("/api/comments/id", CommentAPI::handleDeleteComment);
  svr.Post("/api/comments/id/like", CommentAPI::handleToggleCommentLike);
  svr.Post("/api/comments/detail", CommentAPI::handleGetCommentDetail);

  // 通知接口
  svr.Get("/api/notifications", UserAPI::handleGetNotifications);
  svr.Put("/api/notifications/id/read", UserAPI::handleMarkAsRead);
  svr.Put("/api/notifications/read-all", UserAPI::handleMarkAllAsRead);
  svr.Get("/api/notifications/unread-count", UserAPI::handleGetUnreadCount);

  // 管理员接口
  svr.Post("/api/admin/users", AdminAPI::handleCreateUser);
  svr.Put("/api/admin/users/id", AdminAPI::handleUpdateUser);
  svr.Post("/api/admin/users/handleGetUser", AdminAPI::handleGetUser);
  svr.Put("/api/admin/users/id/password", AdminAPI::handleResetUserPassword);
  svr.Delete("/api/admin/users/id", AdminAPI::handleDeleteUser);
  svr.Post("/api/admin/handleGetAllUsers", AdminAPI::handleGetAllUsers);
  svr.Put("/api/admin/users/id/ban", AdminAPI::handleToggleBanUser);
  svr.Delete("/api/admin/posts/id", AdminAPI::handleForceDeletePost);
  svr.Delete("/api/admin/comments/id", AdminAPI::handleForceDeleteComment);
  svr.Put("/api/admin/posts/id/pin", AdminAPI::handleTogglePinPost);
  svr.Post("/api/admin/posts/deleted", AdminAPI::handleGetDeletedPosts);
  svr.Post("/api/admin/comments/deleted", AdminAPI::handleGetDeletedComments);
  svr.Put("/api/admin/users/id/info", AdminAPI::handleUpdateUserMajorGrade);
  svr.Get("/api/admin/stats", AdminAPI::handleGetStats);
  svr.Put("/api/admin/comments/id/recover", AdminAPI::handleRecoverComment);
  svr.Put("/api/admin/posts/id/recover", AdminAPI::handleRecoverPost);

  std::cout << "Server started at http://127.0.0.1:8080" << std::endl;
  svr.listen("0.0.0.0", 8080);

  sqlite3_close(db);
  return 0;
}
