CREATE DATABASE IF NOT EXISTS ddt_game
    CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE ddt_game;

-- 账号表
CREATE TABLE IF NOT EXISTS accounts (
    id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name          VARCHAR(32) NOT NULL UNIQUE,
    password_hash VARCHAR(64) NOT NULL,
    salt          VARCHAR(32) NOT NULL,
    gender        TINYINT DEFAULT 0,                    -- 0=未选择 1=男 2=女
    created_at    DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE = InnoDB;

-- 玩家档案表
CREATE TABLE IF NOT EXISTS player_profiles (
    account_id BIGINT UNSIGNED PRIMARY KEY,
    nickname   VARCHAR(32),
    level      INT DEFAULT 1,
    exp        INT DEFAULT 0,
    wins       INT DEFAULT 0,
    losses     INT DEFAULT 0,
    FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE = InnoDB;

-- 聊天历史表
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
) ENGINE = InnoDB;

-- 战绩主表(一局对战一条)
DROP TABLE IF EXISTS game_records;
CREATE TABLE game_records (
    id           BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    winning_team TINYINT,                               -- 与 proto TeamSide 数值一致(0=RED 1=BLUE); NULL=无胜方
    duration     INT,
    created_at   DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE = InnoDB;

-- 战绩明细表(每位参战玩家一条)
CREATE TABLE game_record_players (
    id           BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    record_id    BIGINT UNSIGNED NOT NULL,
    account_id   BIGINT UNSIGNED NOT NULL,
    team         TINYINT NOT NULL,                      -- 与 proto TeamSide 数值一致(0=RED 1=BLUE)
    is_winner    TINYINT(1) DEFAULT 0,
    damage_dealt INT DEFAULT 0,
    created_at   DATETIME DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_record (record_id),
    INDEX idx_account (account_id),
    FOREIGN KEY (record_id) REFERENCES game_records(id) ON DELETE CASCADE,
    FOREIGN KEY (account_id) REFERENCES accounts(id)
) ENGINE = InnoDB;

-- 好友关系表
CREATE TABLE IF NOT EXISTS friends (
    account_id BIGINT UNSIGNED,
    friend_id  BIGINT UNSIGNED,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (account_id, friend_id),
    FOREIGN KEY (account_id) REFERENCES accounts(id),
    FOREIGN KEY (friend_id) REFERENCES accounts(id)
) ENGINE = InnoDB;
