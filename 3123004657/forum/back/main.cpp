#include <httplib.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <string>
#include <vector>
#define ADMIN_ACCOUNT "admin"     // 管理员账号
#define ADMIN_PASSWORD "admin123" // 管理员密码
sqlite3 *db = nullptr;
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
        response_json["status"] = "success";
        response_json["message"] = "Login successful";
        response_json["is_admin"] = login_data.is_admin;
        res.status = 200;
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
  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string sql =
      "UPDATE users SET is_online=0 WHERE account='" + account + "';";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    std::cerr << "logout_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Logout failed";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["message"] = "Logout successful";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// GET /api/users/id - 获取用户信息
void handleGetUserInfo(const httplib::Request &req, httplib::Response &res) {
  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string user_account = body["account"];
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
  //获取用户信息
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
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "get_user_info_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve user info";
    res.status = 500;
  } else if (!user_info.found) {
    // 用户不存在
    response_json["status"] = "failure";
    response_json["message"] = "User not found";
    res.status = 404;
  } else {
    // 成功获取用户信息
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
  // json:{"account":"xxx","username":"xxx","bio":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string username = body["username"];
  std::string bio = body["bio"];
  nlohmann::json response_json;

  if (username.empty()) {
    response_json["status"] = "failure";
    response_json["message"] = "Username cannot be empty";
    res.status = 400;
    res.set_content(response_json.dump(), "application/json");
    return;
  }

  std::string sql = "UPDATE users SET username='" + username + "', bio='" +
                    bio + "' WHERE account='" + account + "';";
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
  // json:{"account":"xxx","old_password":"xxx","new_password":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string old_password = body["old_password"];
  std::string new_password = body["new_password"];
  std::string sql =
      "SELECT password FROM users WHERE account='" + account + "';";
  char *err_msg = nullptr;
  struct PasswordData {
    bool success;
    std::string db_password;
  };
  PasswordData password_data;
  password_data.success = false;
  password_data.db_password = "";
  //验证旧密码
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *password_data = static_cast<PasswordData *>(data);
        password_data->db_password = argv[0] ? argv[0] : "";
        return 0;
      },
      &password_data, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "change_password_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to change password";
    res.status = 500;
  } else if (password_data.db_password != old_password) {
    // 旧密码错误
    response_json["status"] = "failure";
    response_json["message"] = "Old password is incorrect";
    res.status = 401;
  } else {
    // 更新为新密码
    std::string update_sql = "UPDATE users SET password='" + new_password +
                             "' WHERE account='" + account + "';";
    rc = sqlite3_exec(db, update_sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      // 更新密码失败
      std::cerr << "update_password_error: " << err_msg << std::endl;
      sqlite3_free(err_msg);
      response_json["status"] = "failure";
      response_json["message"] = "Failed to update password";
      res.status = 500;
    } else {
      // 成功更新密码
      response_json["status"] = "success";
      response_json["message"] = "Password changed successfully";
      res.status = 200;
    }
  }
  res.set_content(response_json.dump(), "application/json");
}
// GET /api/users/id/posts - 获取用户发布的帖子
void handleGetUserPosts(const httplib::Request &req, httplib::Response &res) {
  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string user_account = body["account"];
  std::string sql =
      "SELECT p.post_id, p.title, SUBSTR(p.content, 1, 100) as summary, "
      "u.grade, u.major, u.role, p.category, p.is_anonymous, "
      "p.view_count, p.like_count, p.is_top, p.comment_count, p.created_at "
      "FROM posts p "
      "LEFT JOIN users u ON p.account = u.account "
      "WHERE p.account='" +
      user_account +
      "' AND p.is_deleted=0 "
      "ORDER BY p.created_at DESC;";

  char *err_msg = nullptr;
  struct PostInfo {
    int post_id;
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
  //获取用户帖子
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *posts = static_cast<std::vector<PostInfo> *>(data);
        PostInfo post;
        post.post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post.title = argv[1] ? argv[1] : "";
        post.summary = argv[2] ? argv[2] : "";
        post.grade = argv[3] ? argv[3] : "";
        post.major = argv[4] ? argv[4] : "";
        post.role = argv[5] ? argv[5] : "";
        post.category = argv[6] ? argv[6] : "";
        post.is_anonymous = argv[7] ? std::stoi(argv[7]) : 0;
        post.view_count = argv[8] ? std::stoi(argv[8]) : 0;
        post.like_count = argv[9] ? std::stoi(argv[9]) : 0;
        post.is_top = argv[10] ? std::string(argv[10]) == "1" : false;
        post.comment_count = argv[11] ? std::stoi(argv[11]) : 0;
        post.created_at = argv[12] ? argv[12] : "";
        posts->push_back(post);
        return 0;
      },
      &posts, &err_msg);
  nlohmann::json response_json;
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
      std::string display_account =
          post.is_anonymous ? "匿名用户" : user_account;
      posts_json.push_back({{"post_id", post.post_id},
                            {"account", display_account},
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
  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string user_account = body["account"];
  std::string sql = "SELECT COUNT(*) FROM posts WHERE account='" +
                    user_account + "' AND is_deleted=0;";
  char *err_msg = nullptr;
  int post_count = 0;
  //获取用户帖子数量
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *post_count = static_cast<int *>(data);
        *post_count = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &post_count, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "get_user_post_count_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve user post count";
    res.status = 500;
  } else {
    // 成功获取用户帖子数量
    response_json["status"] = "success";
    response_json["data"] = {{"post_count", post_count}};
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// GET /api/users/id/comments - 获取用户的评论
void handleGetUserComments(const httplib::Request &req,
                           httplib::Response &res) {
  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string user_account = body["account"];
  std::string sql =
      "SELECT comment_id, post_id, content, parent_id, like_count, "
      "created_at FROM comments WHERE account='" +
      user_account + "' AND is_deleted=0;";
  char *err_msg = nullptr;
  struct CommentInfo {
    int comment_id;
    int post_id;
    std::string content;
    int parent_id;
    int like_count;
    std::string created_at;
  };
  std::vector<CommentInfo> comments;
  //获取用户评论
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
        comments->push_back(comment);
        return 0;
      },
      &comments, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "get_user_comments_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve user comments";
    res.status = 500;
  } else {
    // 成功获取用户评论
    response_json["status"] = "success";
    nlohmann::json comments_json = nlohmann::json::array();
    for (const auto &comment : comments) {
      comments_json.push_back({{"comment_id", comment.comment_id},
                               {"post_id", comment.post_id},
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
// GET /api/users/id/favorites - 获取用户收藏的帖子
void handleGetUserFavorites(const httplib::Request &req,
                            httplib::Response &res) {
  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string user_account = body["account"];
  std::string sql =
      "SELECT p.post_id, p.account, p.title, "
      "SUBSTR(p.content, 1, 100) as summary, "
      "u.grade, u.major, u.role, p.category, p.is_anonymous, "
      "p.view_count, p.like_count, p.is_top, p.comment_count, p.created_at "
      "FROM posts p "
      "JOIN user_favorites uf ON p.post_id = uf.post_id "
      "LEFT JOIN users u ON p.account = u.account "
      "WHERE uf.account='" +
      user_account +
      "' AND p.is_deleted=0 "
      "ORDER BY uf.created_at DESC;";

  char *err_msg = nullptr;
  struct PostInfo {
    int post_id;
    std::string account;
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
  //获取用户收藏帖子
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *favorites = static_cast<std::vector<PostInfo> *>(data);
        PostInfo post;
        post.post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post.account = argv[1] ? argv[1] : "";
        post.title = argv[2] ? argv[2] : "";
        post.summary = argv[3] ? argv[3] : "";
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
        favorites->push_back(post);
        return 0;
      },
      &favorites, &err_msg);
  nlohmann::json response_json;
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
      std::string display_account =
          post.is_anonymous ? "匿名用户" : post.account;
      favorites_json.push_back({{"post_id", post.post_id},
                                {"account", display_account},
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
    response_json["data"] = favorites_json;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// GET /api/users/id/likes - 获取用户点赞的帖子
void handleGetUserLikes(const httplib::Request &req, httplib::Response &res) {
  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string user_account = body["account"];
  std::string sql =
      "SELECT p.post_id, p.account, p.title, "
      "SUBSTR(p.content, 1, 100) as summary, "
      "u.grade, u.major, u.role, p.category, p.is_anonymous, "
      "p.view_count, p.like_count, p.is_top, p.comment_count, p.created_at "
      "FROM posts p "
      "JOIN user_likes ul ON p.post_id = ul.post_id "
      "LEFT JOIN users u ON p.account = u.account "
      "WHERE ul.account='" +
      user_account +
      "' AND p.is_deleted=0 "
      "ORDER BY ul.created_at DESC;";

  char *err_msg = nullptr;
  struct PostInfo {
    int post_id;
    std::string account;
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
  //获取用户点赞帖子
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *likes = static_cast<std::vector<PostInfo> *>(data);
        PostInfo post;
        post.post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post.account = argv[1] ? argv[1] : "";
        post.title = argv[2] ? argv[2] : "";
        post.summary = argv[3] ? argv[3] : "";
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
        likes->push_back(post);
        return 0;
      },
      &likes, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "get_user_likes_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve user likes";
    res.status = 500;
  } else {
    // 成功获取用户点赞帖子
    response_json["status"] = "success";
    nlohmann::json likes_json = nlohmann::json::array();
    for (const auto &post : likes) {
      std::string display_account =
          post.is_anonymous ? "匿名用户" : post.account;
      likes_json.push_back({{"post_id", post.post_id},
                            {"account", display_account},
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
    response_json["data"] = likes_json;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// 1111
//  GET /api/notifications - 获取用户的通知列表
void handleGetNotifications(const httplib::Request &req,
                            httplib::Response &res) {
  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string user_account = body["account"];
  std::string sql = "SELECT notification_id, sender_account, type, related_id, "
                    "content, is_read, created_at "
                    "FROM notifications WHERE receiver_account='" +
                    user_account + "' ORDER BY created_at DESC;";
  char *err_msg = nullptr;
  struct NotificationInfo {
    int notification_id;
    std::string sender_account;
    std::string type;
    int related_id;
    std::string content;
    bool is_read;
    std::string created_at;
  };
  std::vector<NotificationInfo> notifications;
  //获取用户通知
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *notifications =
            static_cast<std::vector<NotificationInfo> *>(data);
        NotificationInfo notification;
        notification.notification_id = argv[0] ? std::stoi(argv[0]) : 0;
        notification.sender_account = argv[1] ? argv[1] : "";
        notification.type = argv[2] ? argv[2] : "";
        notification.related_id = argv[3] ? std::stoi(argv[3]) : 0;
        notification.content = argv[4] ? argv[4] : "";
        notification.is_read = argv[5] ? std::string(argv[5]) == "1" : false;
        notification.created_at = argv[6] ? argv[6] : "";
        notifications->push_back(notification);
        return 0;
      },
      &notifications, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "get_notifications_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve notifications";
    res.status = 500;
  } else {
    // 成功获取用户通知
    response_json["status"] = "success";
    nlohmann::json notifications_json = nlohmann::json::array();
    for (const auto &notification : notifications) {
      notifications_json.push_back(
          {{"notification_id", notification.notification_id},
           {"sender_account", notification.sender_account},
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
  // json:{"notification_id":xxx}
  auto body = nlohmann::json::parse(req.body);
  int notification_id = body["notification_id"];
  std::string sql =
      "UPDATE notifications SET is_read=1 WHERE notification_id=" +
      std::to_string(notification_id) + ";";
  char *err_msg = nullptr;
  //标记通知为已读
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "mark_as_read_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to mark notification as read";
    res.status = 500;
  } else {
    // 成功标记通知为已读
    response_json["status"] = "success";
    response_json["message"] = "Notification marked as read";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// PUT /api/notifications/read-all - 标记所有通知为已读
void handleMarkAllAsRead(const httplib::Request &req, httplib::Response &res) {
  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string user_account = body["account"];
  std::string sql =
      "UPDATE notifications SET is_read=1 WHERE receiver_account='" +
      user_account + "';";
  char *err_msg = nullptr;
  //标记所有通知为已读
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "mark_all_as_read_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to mark all notifications as read";
    res.status = 500;
  } else {
    // 成功标记所有通知为已读
    response_json["status"] = "success";
    response_json["message"] = "All notifications marked as read";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// GET /api/notifications/unread-count - 获取未读通知数量
void handleGetUnreadCount(const httplib::Request &req, httplib::Response &res) {
  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string user_account = body["account"];
  std::string sql =
      "SELECT COUNT(*) FROM notifications WHERE receiver_account='" +
      user_account + "' AND is_read=0;";
  char *err_msg = nullptr;
  int unread_count = 0;
  //获取未读通知数量
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *unread_count = static_cast<int *>(data);
        *unread_count = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &unread_count, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "get_unread_count_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve unread notification count";
    res.status = 500;
  } else {
    // 成功获取未读通知数量
    response_json["status"] = "success";
    response_json["data"] = {{"unread_count", unread_count}};
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// 1111
//  GET /api/users/id/comment-likes - 获取用户点赞的评论
void handleGetUserCommentLikes(const httplib::Request &req,
                               httplib::Response &res) {
  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string user_account = body["account"];
  std::string sql = "SELECT c.comment_id, c.post_id, c.account, c.content, "
                    "COALESCE(c.parent_id, 0) as parent_id, "
                    "c.like_count, c.created_at "
                    "FROM comments c "
                    "JOIN comment_likes cl ON c.comment_id = cl.comment_id "
                    "WHERE cl.account='" +
                    user_account +
                    "' AND c.is_deleted=0 "
                    "ORDER BY cl.created_at DESC;";

  char *err_msg = nullptr;
  struct CommentInfo {
    int comment_id;
    int post_id;
    std::string account;
    std::string content;
    int parent_id;
    int like_count;
    std::string created_at;
  };
  std::vector<CommentInfo> liked_comments;
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *liked_comments = static_cast<std::vector<CommentInfo> *>(data);
        CommentInfo comment;
        comment.comment_id = argv[0] ? std::stoi(argv[0]) : 0;
        comment.post_id = argv[1] ? std::stoi(argv[1]) : 0;
        comment.account = argv[2] ? argv[2] : "";
        comment.content = argv[3] ? argv[3] : "";
        comment.parent_id = argv[4] ? std::stoi(argv[4]) : 0;
        comment.like_count = argv[5] ? std::stoi(argv[5]) : 0;
        comment.created_at = argv[6] ? argv[6] : "";
        liked_comments->push_back(comment);
        return 0;
      },
      &liked_comments, &err_msg);

  nlohmann::json response_json;
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
      comments_json.push_back({{"comment_id", comment.comment_id},
                               {"post_id", comment.post_id},
                               {"account", comment.account},
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
  // json:{"keyword":"abc","page":1,"page_size":10}
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
// GET /api/posts - 获取帖子列表，置顶优先，其次时间降序，支持分页和筛选
void handleGetPosts(const httplib::Request &req, httplib::Response &res) {
  // json:{"page":1,"page_size":10,"major":"xxx","grade":"xxx","role":"xxx","category":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  int page = body["page"];
  int page_size = body["page_size"];
  int offset = (page - 1) * page_size;
  std::string major = body.value("major", "");
  std::string grade = body.value("grade", "");
  std::string role = body.value("role", "");
  std::string category = body.value("category", "");

  std::string sql =
      "SELECT p.post_id, p.account, p.title, SUBSTR(p.content, 1, 100) as "
      "summary, "
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

  //获取帖子列表
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *posts = static_cast<std::vector<PostInfo> *>(data);
        PostInfo post;
        post.post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post.account = argv[1] ? argv[1] : "";
        post.title = argv[2] ? argv[2] : "";
        post.summary = argv[3] ? argv[3] : "";
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

  nlohmann::json response_json;
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
      std::string display_account =
          post.is_anonymous ? "匿名用户" : post.account;
      posts_json.push_back({{"post_id", post.post_id},
                            {"account", display_account},
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
// GET /api/posts/id - 获取单个帖子详情，浏览量增加
void handleGetPostDetail(const httplib::Request &req, httplib::Response &res) {
  // json:{"post_id":xxx}
  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  char *err_msg = nullptr;
  //先更新浏览量
  std::string update_sql =
      "UPDATE posts SET view_count=view_count+1 WHERE post_id=" +
      std::to_string(post_id) + " AND is_deleted=0;";
  int rc = sqlite3_exec(db, update_sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::cerr << "update_view_count_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    nlohmann::json response_json;
    response_json["status"] = "failure";
    response_json["message"] = "Failed to update view count";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  // 检查是否有行被更新（判断帖子是否存在且未删除）
  if (sqlite3_changes(db) == 0) {
    nlohmann::json response_json;
    response_json["status"] = "failure";
    response_json["message"] = "Post not found or has been deleted";
    res.status = 404;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  std::string sql = "SELECT p.post_id, p.account, p.title, p.content, "
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
  //获取帖子详情
  rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *post_data = static_cast<std::pair<PostInfo *, bool *> *>(data);
        PostInfo *post = post_data->first;
        *(post_data->second) = true;
        post->post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post->account = argv[1] ? argv[1] : "";
        post->title = argv[2] ? argv[2] : "";
        post->content = argv[3] ? argv[3] : "";
        post->grade = argv[4] ? argv[4] : "";
        post->major = argv[5] ? argv[5] : "";
        post->role = argv[6] ? argv[6] : "";
        post->category = argv[7] ? argv[7] : "";
        post->is_anonymous = argv[8] ? std::stoi(argv[8]) : 0;
        post->view_count = argv[9] ? std::stoi(argv[9]) : 0;
        post->like_count = argv[10] ? std::stoi(argv[10]) : 0;
        post->is_top = argv[11] ? std::string(argv[11]) == "1" : false;
        post->comment_count = argv[12] ? std::stoi(argv[12]) : 0;
        post->created_at = argv[13] ? argv[13] : "";
        post->updated_at = argv[14] ? argv[14] : "";
        return 0;
      },
      &post_data_pair, &err_msg);
  nlohmann::json response_json;
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
    std::string display_account = post.is_anonymous ? "匿名用户" : post.account;
    response_json["status"] = "success";
    response_json["data"] = {{"post_id", post.post_id},
                             {"account", display_account},
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
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/posts - 创建新帖子
void handleCreatePost(const httplib::Request &req, httplib::Response &res) {
  // json:{"account":"xxx","title":"xxx","content":"xxx","category":"xxx","is_anonymous":0}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string title = body["title"];
  std::string content = body["content"];
  std::string category = body.value("category", "");
  int is_anonymous = body.value("is_anonymous", 0);
  std::string sql =
      "INSERT INTO posts (account, title, content, category, is_anonymous, "
      "view_count, like_count, is_top, comment_count, is_deleted, created_at, "
      "updated_at) VALUES ('" +
      account + "', '" + title + "', '" + content + "', " +
      (category.empty() ? "NULL" : ("'" + category + "'")) + ", " +
      std::to_string(is_anonymous) +
      ", 0, 0, 0, 0, 0, datetime('now'), datetime('now'));";
  char *err_msg = nullptr;
  //创建新帖子
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  nlohmann::json response_json;
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
  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  std::string title = body["title"];
  std::string content = body["content"];
  std::string sql =
      "UPDATE posts SET title='" + title + "', content='" + content +
      "', updated_at=datetime('now') WHERE post_id=" + std::to_string(post_id) +
      " AND is_deleted=0;";
  char *err_msg = nullptr;
  //更新帖子内容标题时间
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "update_post_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to update post";
    res.status = 500;
  } else {
    // 成功更新帖子
    response_json["status"] = "success";
    response_json["message"] = "Post updated successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// DELETE /api/posts/id - 软删除帖子
void handleDeletePost(const httplib::Request &req, httplib::Response &res) {
  // json:{"post_id":xxx}
  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  std::string sql =
      "UPDATE posts SET is_deleted=1 WHERE post_id=" + std::to_string(post_id) +
      ";";
  char *err_msg = nullptr;
  //软删除帖子
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "delete_post_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to delete post";
    res.status = 500;
  } else {
    // 成功软删除帖子
    response_json["status"] = "success";
    response_json["message"] = "Post deleted successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
//  GET /api/posts/hot - 获取热门帖子
void handleGetHotPosts(const httplib::Request &req, httplib::Response &res) {
  std::string sql =
      "SELECT p.post_id, p.account, p.title, "
      "SUBSTR(p.content, 1, 100) as summary, "
      "u.grade, u.major, u.role, p.category, p.is_anonymous, "
      "p.view_count, p.like_count, p.is_top, p.comment_count, p.created_at "
      "FROM posts p "
      "LEFT JOIN users u ON p.account = u.account "
      "WHERE p.is_deleted=0 "
      "ORDER BY p.view_count DESC, p.like_count DESC "
      "LIMIT 10;";

  char *err_msg = nullptr;
  struct PostInfo {
    int post_id;
    std::string account;
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

  //获取热门帖子
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *posts = static_cast<std::vector<PostInfo> *>(data);
        PostInfo post;
        post.post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post.account = argv[1] ? argv[1] : "";
        post.title = argv[2] ? argv[2] : "";
        post.summary = argv[3] ? argv[3] : "";
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

  nlohmann::json response_json;
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
      std::string display_account =
          post.is_anonymous ? "匿名用户" : post.account;
      posts_json.push_back({{"post_id", post.post_id},
                            {"account", display_account},
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
// GET /api/posts/pinned - 获取置顶帖子
void handleGetPinnedPosts(const httplib::Request &req, httplib::Response &res) {
  std::string sql =
      "SELECT p.post_id, p.account, p.title, SUBSTR(p.content, 1, 100) as "
      "summary, "
      "u.grade, u.major, u.role, p.category, p.is_anonymous, "
      "p.view_count, p.like_count, p.is_top, p.comment_count, p.created_at "
      "FROM posts p "
      "LEFT JOIN users u ON p.account = u.account "
      "WHERE p.is_deleted=0 AND p.is_top=1 "
      "ORDER BY p.created_at DESC;";

  char *err_msg = nullptr;
  struct PostInfo {
    int post_id;
    std::string account;
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
  //获取置顶帖子
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *posts = static_cast<std::vector<PostInfo> *>(data);
        PostInfo post;
        post.post_id = argv[0] ? std::stoi(argv[0]) : 0;
        post.account = argv[1] ? argv[1] : "";
        post.title = argv[2] ? argv[2] : "";
        post.summary = argv[3] ? argv[3] : "";
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
  nlohmann::json response_json;
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
      std::string display_account =
          post.is_anonymous ? "匿名用户" : post.account;
      posts_json.push_back({{"post_id", post.post_id},
                            {"account", display_account},
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
//  POST /api/posts/id/like - 点赞/取消点赞
void handleToggleLike(const httplib::Request &req, httplib::Response &res) {
  // json:{"account":"xxx","post_id":xxx}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  int post_id = body["post_id"];
  //检查用户是否已点赞该帖子
  std::string check_sql = "SELECT COUNT(*) FROM user_likes WHERE account='" +
                          account + "' AND post_id=" + std::to_string(post_id) +
                          ";";
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
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
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
        "DELETE FROM user_likes WHERE account='" + account +
        "' AND post_id=" + std::to_string(post_id) +
        ";"
        "UPDATE posts SET like_count=like_count-1 WHERE post_id=" +
        std::to_string(post_id) + ";";
    rc = sqlite3_exec(db, delete_sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      // SQL 执行错误
      std::cerr << "toggle_like_unlike_error: " << err_msg << std::endl;
      sqlite3_free(err_msg);
      response_json["status"] = "failure";
      response_json["message"] = "Failed to unlike post";
      res.status = 500;
    } else {
      // 成功取消点赞
      response_json["status"] = "success";
      response_json["message"] = "Post unliked successfully";
      res.status = 200;
    }
  } else {
    // 用户未点赞，执行点赞
    std::string insert_sql =
        "INSERT INTO user_likes (account, post_id) VALUES ('" + account +
        "', " + std::to_string(post_id) +
        ");"
        "UPDATE posts SET like_count=like_count+1 WHERE post_id=" +
        std::to_string(post_id) + ";";
    rc = sqlite3_exec(db, insert_sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      // SQL 执行错误
      std::cerr << "toggle_like_like_error: " << err_msg << std::endl;
      sqlite3_free(err_msg);
      response_json["status"] = "failure";
      response_json["message"] = "Failed to like post";
      res.status = 500;
    } else {
      // 成功点赞
      response_json["status"] = "success";
      response_json["message"] = "Post liked successfully";
      res.status = 200;
    }
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/posts/id/favorite - 收藏/取消收藏
void handleToggleFavorite(const httplib::Request &req, httplib::Response &res) {
  // json:{"account":"xxx","post_id":xxx}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  int post_id = body["post_id"];
  //检查用户是否已收藏该帖子
  std::string check_sql =
      "SELECT COUNT(*) FROM user_favorites WHERE account='" + account +
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
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
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
                             account +
                             "' AND post_id=" + std::to_string(post_id) + ";";
    rc = sqlite3_exec(db, delete_sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      // SQL 执行错误
      std::cerr << "toggle_favorite_unfavorite_error: " << err_msg << std::endl;
      sqlite3_free(err_msg);
      response_json["status"] = "failure";
      response_json["message"] = "Failed to unfavorite post";
      res.status = 500;
    } else {
      // 成功取消收藏
      response_json["status"] = "success";
      response_json["message"] = "Post unfavorited successfully";
      res.status = 200;
    }
  } else {
    // 用户未收藏，执行收藏
    std::string insert_sql =
        "INSERT INTO user_favorites (account, post_id) VALUES ('" + account +
        "', " + std::to_string(post_id) + ");";
    rc = sqlite3_exec(db, insert_sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      // SQL 执行错误
      std::cerr << "toggle_favorite_favorite_error: " << err_msg << std::endl;
      sqlite3_free(err_msg);
      response_json["status"] = "failure";
      response_json["message"] = "Failed to favorite post";
      res.status = 500;
    } else {
      // 成功收藏
      response_json["status"] = "success";
      response_json["message"] = "Post favorited successfully";
      res.status = 200;
    }
    res.set_content(response_json.dump(), "application/json");
  }
}
// GET /api/posts/search?q=关键词 - 搜索帖子
void handleSearchPosts(const httplib::Request &req, httplib::Response &res) {
  // GET /api/posts/search?q=关键词&page=1&page_size=10
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
  std::string sql =
      "SELECT p.post_id, p.account, p.title, SUBSTR(p.content, 1, 100) as "
      "summary, "
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
        post.title = argv[2] ? argv[2] : "";
        post.summary = argv[3] ? argv[3] : "";
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
    std::cerr << "search_posts_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to search posts";
    res.status = 500;
  } else {
    response_json["status"] = "success";
    response_json["data"] = nlohmann::json::array();
    for (const auto &post : posts) {
      std::string display_account =
          post.is_anonymous ? "匿名用户" : post.account;
      response_json["data"].push_back({{"post_id", post.post_id},
                                       {"account", display_account},
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
    response_json["total_results"] = posts.size();
    response_json["keyword"] = keyword;
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
} // namespace PostAPI
//评论接口
namespace CommentAPI {
// GET /api/posts/post_id/comments - 获取帖子的评论列表，时间排序
void handleGetComments(const httplib::Request &req, httplib::Response &res) {
  // json:{"post_id":xxx,"page":1,"page_size":10}
  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  int page = body["page"];
  int page_size = body["page_size"];
  int offset = (page - 1) * page_size;
  std::string sql = "SELECT comment_id, account, content,"
                    "COALESCE(parent_id, 0) as parent_id, "
                    "like_count, created_at "
                    "FROM comments WHERE post_id=" +
                    std::to_string(post_id) +
                    " AND is_deleted=0 "
                    " ORDER BY created_at DESC "
                    "LIMIT " +
                    std::to_string(page_size) + " OFFSET " +
                    std::to_string(offset) + ";";
  char *err_msg = nullptr;
  struct CommentInfo {
    int comment_id;
    std::string account;
    std::string content;
    int parent_id;
    int like_count;
    std::string created_at;
  };
  std::vector<CommentInfo> comments;
  //获取评论列表
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *comments = static_cast<std::vector<CommentInfo> *>(data);
        CommentInfo comment;
        comment.comment_id = argv[0] ? std::stoi(argv[0]) : 0;
        comment.account = argv[1] ? argv[1] : "";
        comment.content = argv[2] ? argv[2] : "";
        comment.parent_id = argv[3] ? std::stoi(argv[3]) : 0;
        comment.like_count = argv[4] ? std::stoi(argv[4]) : 0;
        comment.created_at = argv[5] ? argv[5] : "";
        comments->push_back(comment);
        return 0;
      },
      &comments, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "get_comments_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve comments";
    res.status = 500;
  } else {
    // 成功获取评论列表
    response_json["status"] = "success";
    nlohmann::json comments_json = nlohmann::json::array();
    for (const auto &comment : comments) {
      comments_json.push_back({{"comment_id", comment.comment_id},
                               {"account", comment.account},
                               {"content", comment.content},
                               {"parent_id", comment.parent_id},
                               {"like_count", comment.like_count},
                               {"created_at", comment.created_at}});
    }
    response_json["data"] = comments_json;
    std::string count_sql = "SELECT COUNT(*) FROM comments WHERE post_id=" +
                            std::to_string(post_id) + " AND is_deleted=0;";
    int total_comments = 0;
    sqlite3_exec(
        db, count_sql.c_str(),
        [](void *data, int argc, char **argv, char **azColName) -> int {
          auto *count = static_cast<int *>(data);
          *count = argv[0] ? std::stoi(argv[0]) : 0;
          return 0;
        },
        &total_comments, nullptr);
    response_json["pagination"] = {
        {"current_page", page},
        {"page_size", page_size},
        {"total_comments", total_comments},
        {"total_pages", (total_comments + page_size - 1) / page_size}};
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// GET /api/posts/post_id/comments/likes - 获取帖子的评论列表，点赞数排序
void handleGetCommentsByLikes(const httplib::Request &req,
                              httplib::Response &res) {
  // json:{"post_id":xxx,"page":1,"page_size":10}
  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  int page = body["page"];
  int page_size = body["page_size"];
  int offset = (page - 1) * page_size;
  std::string sql = "SELECT comment_id, account, content,"
                    "COALESCE(parent_id, 0) as parent_id, "
                    "like_count, created_at "
                    "FROM comments WHERE post_id=" +
                    std::to_string(post_id) +
                    " AND is_deleted=0 "
                    " ORDER BY like_count DESC, created_at DESC "
                    "LIMIT " +
                    std::to_string(page_size) + " OFFSET " +
                    std::to_string(offset) + ";";
  char *err_msg = nullptr;
  struct CommentInfo {
    int comment_id;
    std::string account;
    std::string content;
    int parent_id;
    int like_count;
    std::string created_at;
  };
  std::vector<CommentInfo> comments;
  //获取评论列表
  int rc = sqlite3_exec(
      db, sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *comments = static_cast<std::vector<CommentInfo> *>(data);
        CommentInfo comment;
        comment.comment_id = argv[0] ? std::stoi(argv[0]) : 0;
        comment.account = argv[1] ? argv[1] : "";
        comment.content = argv[2] ? argv[2] : "";
        comment.parent_id = argv[3] ? std::stoi(argv[3]) : 0;
        comment.like_count = argv[4] ? std::stoi(argv[4]) : 0;
        comment.created_at = argv[5] ? argv[5] : "";
        comments->push_back(comment);
        return 0;
      },
      &comments, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "get_comments_by_likes_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to retrieve comments";
    res.status = 500;
  } else {
    // 成功获取评论列表
    response_json["status"] = "success";
    nlohmann::json comments_json = nlohmann::json::array();
    for (const auto &comment : comments) {
      comments_json.push_back({{"comment_id", comment.comment_id},
                               {"account", comment.account},
                               {"content", comment.content},
                               {"parent_id", comment.parent_id},
                               {"like_count", comment.like_count},
                               {"created_at", comment.created_at}});
    }
    response_json["data"] = comments_json;
    std::string count_sql = "SELECT COUNT(*) FROM comments WHERE post_id=" +
                            std::to_string(post_id) + " AND is_deleted=0;";
    int total_comments = 0;
    sqlite3_exec(
        db, count_sql.c_str(),
        [](void *data, int argc, char **argv, char **azColName) -> int {
          auto *count = static_cast<int *>(data);
          *count = argv[0] ? std::stoi(argv[0]) : 0;
          return 0;
        },
        &total_comments, nullptr);
    response_json["pagination"] = {
        {"current_page", page},
        {"page_size", page_size},
        {"total_comments", total_comments},
        {"total_pages", (total_comments + page_size - 1) / page_size}};
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/posts/post_id/comments - 发表评论
void handleCreateComment(const httplib::Request &req, httplib::Response &res) {
  // json:{"post_id":xxx,"account":"xxx","content":"xxx","parent_id":yyy}
  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  std::string account = body["account"];
  std::string content = body["content"];
  int parent_id = body.value("parent_id", 0);
  std::string sql;
  if (parent_id == 0) {
    // 顶层评论：parent_id 设为 NULL
    sql = "INSERT INTO comments (post_id, account, content, parent_id, "
          "like_count, is_deleted, created_at) "
          "VALUES (" +
          std::to_string(post_id) + ", '" + account + "', '" + content +
          "', NULL, 0, 0, datetime('now'));";
  } else {
    // 回复评论：parent_id 设为具体数字
    sql = "INSERT INTO comments (post_id, account, content, parent_id, "
          "like_count, is_deleted, created_at) "
          "VALUES (" +
          std::to_string(post_id) + ", '" + account + "', '" + content + "', " +
          std::to_string(parent_id) + ", 0, 0, datetime('now'));";
  }
  char *err_msg = nullptr;
  //创建新评论
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "create_comment_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to create comment";
    res.status = 500;
  } else {
    // 更新帖子的评论数
    std::string update_sql =
        "UPDATE posts SET comment_count=comment_count+1 WHERE post_id=" +
        std::to_string(post_id) + ";";
    sqlite3_exec(db, update_sql.c_str(), nullptr, nullptr, nullptr);
    // 成功创建新评论
    response_json["status"] = "success";
    response_json["message"] = "Comment created successfully";
    res.status = 201;
  }
  res.set_content(response_json.dump(), "application/json");
}
// DELETE /api/comments/id - 删除评论
void handleDeleteComment(const httplib::Request &req, httplib::Response &res) {
  // json:{"comment_id":xxx}
  auto body = nlohmann::json::parse(req.body);
  int comment_id = body["comment_id"];
  //获取评论所属的post_id
  std::string get_post_sql = "SELECT post_id FROM comments WHERE comment_id=" +
                             std::to_string(comment_id) + ";";
  char *err_msg = nullptr;
  int post_id = 0;
  int rc = sqlite3_exec(
      db, get_post_sql.c_str(),
      [](void *data, int argc, char **argv, char **azColName) -> int {
        auto *post_id = static_cast<int *>(data);
        *post_id = argv[0] ? std::stoi(argv[0]) : 0;
        return 0;
      },
      &post_id, &err_msg);
  nlohmann::json response_json;
  if (rc != SQLITE_OK || post_id == 0) {
    // SQL 执行错误或评论不存在
    std::cerr << "delete_comment_get_post_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to delete comment";
    res.status = 500;
    res.set_content(response_json.dump(), "application/json");
    return;
  }
  //软删除评论
  std::string sql = "UPDATE comments SET is_deleted=1 WHERE comment_id=" +
                    std::to_string(comment_id) + ";";
  rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    // SQL 执行错误
    std::cerr << "delete_comment_error: " << err_msg << std::endl;
    sqlite3_free(err_msg);
    response_json["status"] = "failure";
    response_json["message"] = "Failed to delete comment";
    res.status = 500;
  } else {
    // 更新帖子的评论数
    std::string update_sql =
        "UPDATE posts SET comment_count=comment_count-1 WHERE post_id=" +
        std::to_string(post_id) + ";";
    sqlite3_exec(db, update_sql.c_str(), nullptr, nullptr, nullptr);
    // 成功软删除评论
    response_json["status"] = "success";
    response_json["message"] = "Comment deleted successfully";
    res.status = 200;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/comments/id/like - 点赞/取消点赞评论
void handleToggleCommentLike(const httplib::Request &req,
                             httplib::Response &res) {
  // json:{"account":"xxx","comment_id":xxx}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  int comment_id = body["comment_id"];
  //检查用户是否已点赞该评论
  std::string check_sql =
      "SELECT COUNT(*) FROM comment_likes WHERE account='" + account +
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
  nlohmann::json response_json;
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
        "DELETE FROM comment_likes WHERE account='" + account +
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
        "INSERT INTO comment_likes (account, comment_id) VALUES ('" + account +
        "', " + std::to_string(comment_id) +
        ");"
        "UPDATE comments SET like_count=like_count+1 WHERE comment_id=" +
        std::to_string(comment_id) + ";";
    rc = sqlite3_exec(db, insert_sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      std::cerr << "toggle_comment_like_like_error: " << err_msg << std::endl;
      sqlite3_free(err_msg);
      response_json["status"] = "failure";
      response_json["message"] = "Failed to like comment";
      res.status = 500;
    } else {
      response_json["status"] = "success";
      response_json["message"] = "Comment liked successfully";
      res.status = 200;
    }
  }
  res.set_content(response_json.dump(), "application/json");
}
} // namespace CommentAPI
//管理员接口
namespace AdminAPI {
// POST /api/admin/login - 超级管理员登录
void handleAdminLogin(const httplib::Request &req, httplib::Response &res) {
  // json:{"account":"xxx","password":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string password = body["password"];
  nlohmann::json response_json;

  // 只允许账号和密码都为超级管理员常量
  if (account == ADMIN_ACCOUNT && password == ADMIN_PASSWORD) {
    response_json["status"] = "success";
    response_json["message"] = "Admin login successful";
    response_json["is_admin"] = true;
    res.status = 200;
  } else {
    response_json["status"] = "failure";
    response_json["message"] = "Invalid admin credentials";
    res.status = 401;
  }
  res.set_content(response_json.dump(), "application/json");
}
// POST /api/admin/users - 创建新用户
void handleCreateUser(const httplib::Request &req, httplib::Response &res) {
  // json:{"account":"xxx","username":"xxx","password":"xxx","major":"xxx","grade":"xxx","role":"xxx","is_admin":0}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string username = body["username"];
  std::string password = body["password"];
  std::string role = body.value("role", "student"); // 默认学生
  std::string major = body.value("major", "");
  std::string grade = body.value("grade", "");
  int is_admin = body.value("is_admin", 0);
  nlohmann::json response_json;
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
  // json:{"account":"xxx","is_admin":1}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  int is_admin = body.value("is_admin", 1); // 默认为1，支持0取消

  std::string sql = "UPDATE users SET is_admin=" + std::to_string(is_admin) +
                    " WHERE account='" + account + "';";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  nlohmann::json response_json;
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
// GET /api/admin/users/id - 获取用户所有信息
void handleGetUser(const httplib::Request &req, httplib::Response &res) {
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
  nlohmann::json response_json;
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
  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string new_password = "123456";
  std::string sql = "UPDATE users SET password='" + new_password +
                    "' WHERE account='" + account + "';";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  nlohmann::json response_json;
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
  // json:{"account":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string sql = "DELETE FROM users WHERE account='" + account + "';";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  nlohmann::json response_json;
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
// GET /api/admin/users - 获取所有用户（account排序）
void handleGetAllUsers(const httplib::Request &req, httplib::Response &res) {
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

  nlohmann::json response_json;
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
  nlohmann::json response_json;
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
  std::string sql = "DELETE FROM posts WHERE is_deleted=1;";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  nlohmann::json response_json;
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
  std::string sql = "DELETE FROM comments WHERE is_deleted=1;";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  nlohmann::json response_json;
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

  nlohmann::json response_json;
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

  nlohmann::json response_json;
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
// GET /api/admin/posts/deleted - 查看所有软删除的帖子
void handleGetDeletedPosts(const httplib::Request &req,
                           httplib::Response &res) {
  int page = 1, page_size = 10;
  if (!req.body.empty()) {
    auto body = nlohmann::json::parse(req.body);
    page = body.value("page", 1);
    page_size = body.value("page_size", 10);
  }
  int offset = (page - 1) * page_size;

  // 通过 JOIN 查询发帖人的身份信息
  std::string sql =
      "SELECT p.post_id, p.account, p.title, SUBSTR(p.content, 1, 100) as "
      "summary, "
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
        post.title = argv[2] ? argv[2] : "";
        post.summary = argv[3] ? argv[3] : "";
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

  nlohmann::json response_json;
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
      std::string display_account =
          post.is_anonymous ? "匿名用户" : post.account;
      posts_json.push_back({{"post_id", post.post_id},
                            {"account", display_account},
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
// GET /api/admin/comments/deleted - 查看所有软删除的评论
void handleGetDeletedComments(const httplib::Request &req,
                              httplib::Response &res) {
  // json:{"page":1,"page_size":10}
  int page = 1, page_size = 10;
  if (!req.body.empty()) {
    auto body = nlohmann::json::parse(req.body);
    page = body.value("page", 1);
    page_size = body.value("page_size", 10);
  }
  int offset = (page - 1) * page_size;

  std::string sql = "SELECT comment_id, post_id, account, content, "
                    "COALESCE(parent_id, 0) as parent_id, "
                    "like_count, created_at "
                    "FROM comments WHERE is_deleted=1 "
                    "ORDER BY created_at DESC "
                    "LIMIT " +
                    std::to_string(page_size) + " OFFSET " +
                    std::to_string(offset) + ";";
  char *err_msg = nullptr;
  struct CommentInfo {
    int comment_id;
    int post_id;
    std::string account;
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
        comment.content = argv[3] ? argv[3] : "";
        comment.parent_id = argv[4] ? std::stoi(argv[4]) : 0;
        comment.like_count = argv[5] ? std::stoi(argv[5]) : 0;
        comment.created_at = argv[6] ? argv[6] : "";
        comments->push_back(comment);
        return 0;
      },
      &comments, &err_msg);

  nlohmann::json response_json;
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
  // json:{"comment_id":xxx}
  auto body = nlohmann::json::parse(req.body);
  int comment_id = body["comment_id"];
  std::string sql = "UPDATE comments SET is_deleted=0 WHERE comment_id=" +
                    std::to_string(comment_id) + ";";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  nlohmann::json response_json;
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
  // json:{"post_id":xxx}
  auto body = nlohmann::json::parse(req.body);
  int post_id = body["post_id"];
  std::string sql =
      "UPDATE posts SET is_deleted=0 WHERE post_id=" + std::to_string(post_id) +
      ";";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);

  nlohmann::json response_json;
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
  // json:{"account":"xxx","major":"xxx","grade":"xxx"}
  auto body = nlohmann::json::parse(req.body);
  std::string account = body["account"];
  std::string major = body.value("major", "");
  std::string grade = body.value("grade", "");
  std::string sql = "UPDATE users SET major='" + major + "', grade='" + grade +
                    "' WHERE account='" + account + "';";
  char *err_msg = nullptr;
  int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
  nlohmann::json response_json;
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

  // 帖子接口
  svr.Post("/api/posts", PostAPI::handleCreatePost);
  svr.Get("/api/posts", PostAPI::handleGetPosts);
  svr.Get("/api/posts/id", PostAPI::handleGetPostDetail);
  svr.Put("/api/posts/id", PostAPI::handleUpdatePost);
  svr.Delete("/api/posts/id", PostAPI::handleDeletePost);
  svr.Get("/api/posts/hot", PostAPI::handleGetHotPosts);
  svr.Get("/api/posts/pinned", PostAPI::handleGetPinnedPosts);
  svr.Post("/api/posts/id/like", PostAPI::handleToggleLike);
  svr.Post("/api/posts/id/favorite", PostAPI::handleToggleFavorite);
  svr.Get("/api/posts/search", PostAPI::handleSearchPosts);

  // 评论接口
  svr.Get("/api/posts/post_id/comments", CommentAPI::handleGetComments);
  svr.Get("/api/posts/post_id/comments/likes",
          CommentAPI::handleGetCommentsByLikes);
  svr.Post("/api/posts/post_id/comments", CommentAPI::handleCreateComment);
  svr.Delete("/api/comments/id", CommentAPI::handleDeleteComment);
  svr.Post("/api/comments/id/like", CommentAPI::handleToggleCommentLike);

  // 通知接口
  svr.Get("/api/notifications", UserAPI::handleGetNotifications);
  svr.Put("/api/notifications/id/read", UserAPI::handleMarkAsRead);
  svr.Put("/api/notifications/read-all", UserAPI::handleMarkAllAsRead);
  svr.Get("/api/notifications/unread-count", UserAPI::handleGetUnreadCount);

  // 管理员接口
  svr.Post("/api/admin/login", AdminAPI::handleAdminLogin);
  svr.Post("/api/admin/users", AdminAPI::handleCreateUser);
  svr.Put("/api/admin/users/id", AdminAPI::handleUpdateUser);
  svr.Get("/api/admin/users/id", AdminAPI::handleGetUser);
  svr.Put("/api/admin/users/id/password", AdminAPI::handleResetUserPassword);
  svr.Delete("/api/admin/users/id", AdminAPI::handleDeleteUser);
  svr.Get("/api/admin/users", AdminAPI::handleGetAllUsers);
  svr.Put("/api/admin/users/id/ban", AdminAPI::handleToggleBanUser);
  svr.Delete("/api/admin/posts/id", AdminAPI::handleForceDeletePost);
  svr.Delete("/api/admin/comments/id", AdminAPI::handleForceDeleteComment);
  svr.Put("/api/admin/posts/id/pin", AdminAPI::handleTogglePinPost);
  svr.Get("/api/admin/posts/deleted", AdminAPI::handleGetDeletedPosts);
  svr.Get("/api/admin/comments/deleted", AdminAPI::handleGetDeletedComments);
  svr.Put("/api/admin/users/id/info", AdminAPI::handleUpdateUserMajorGrade);
  svr.Get("/api/admin/stats", AdminAPI::handleGetStats);
  svr.Put("/api/admin/comments/id/recover", AdminAPI::handleRecoverComment);
  svr.Put("/api/admin/posts/id/recover", AdminAPI::handleRecoverPost);

  std::cout << "Server started at http://127.0.0.1:8080" << std::endl;
  svr.listen("0.0.0.0", 8080);

  sqlite3_close(db);
  return 0;
}
