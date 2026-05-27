CREATE DATABASE IF NOT EXISTS ddt_game
  CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE ddt_game;

CREATE TABLE IF NOT EXISTS accounts (
  id             BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  name           VARCHAR(32)  NOT NULL UNIQUE,
  password_hash  VARCHAR(64)  NOT NULL,
  salt           VARCHAR(32)  NOT NULL,
  created_at     DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS player_profiles (
  account_id  BIGINT UNSIGNED PRIMARY KEY,
  nickname    VARCHAR(32),
  level       INT DEFAULT 1,
  exp         INT DEFAULT 0,
  wins        INT DEFAULT 0,
  losses      INT DEFAULT 0,
  FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS chat_history (
  id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  channel     TINYINT NOT NULL,
  sender_id   BIGINT UNSIGNED NOT NULL,
  sender_name VARCHAR(32),
  message     TEXT,
  target_id   BIGINT UNSIGNED DEFAULT 0,
  created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
  INDEX idx_channel_time (channel, created_at),
  INDEX idx_private (sender_id, target_id, created_at)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS game_records (
  id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  player1_id  BIGINT UNSIGNED,
  player2_id  BIGINT UNSIGNED,
  winner_id   BIGINT UNSIGNED DEFAULT 0,
  duration    INT,
  created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS friends (
  account_id  BIGINT UNSIGNED,
  friend_id   BIGINT UNSIGNED,
  created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (account_id, friend_id),
  FOREIGN KEY (account_id) REFERENCES accounts(id),
  FOREIGN KEY (friend_id) REFERENCES accounts(id)
) ENGINE=InnoDB;
