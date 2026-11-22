-- Migration: create user_book_progress table
-- Run this against your bronxbot database

CREATE TABLE IF NOT EXISTS user_book_progress (
    user_id         BIGINT UNSIGNED NOT NULL,
    book_id         INT             NOT NULL,
    current_page    INT             NOT NULL DEFAULT 0,
    last_read_at    TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, book_id),
    CONSTRAINT fk_ubp_user FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
