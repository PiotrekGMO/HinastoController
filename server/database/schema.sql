-- Hinasto Controller server, MySQL / MariaDB.
-- {p} is replaced by the table prefix chosen during setup (so the database can be shared with other applications).

CREATE TABLE IF NOT EXISTS {p}devices (
  id           INT AUTO_INCREMENT PRIMARY KEY,
  device_id    VARCHAR(64)  NOT NULL UNIQUE,
  secret_hash  CHAR(64)     NOT NULL,
  status       ENUM('pending','approved','rejected') NOT NULL DEFAULT 'pending',
  name         VARCHAR(80)  NOT NULL DEFAULT '',
  fw           VARCHAR(40)  NOT NULL DEFAULT '',
  ip           VARCHAR(45)  NOT NULL DEFAULT '',
  first_seen   DATETIME     NOT NULL,
  last_seen    DATETIME     NOT NULL,
  approved_at  DATETIME     NULL,
  state_json   MEDIUMTEXT   NULL,
  state_at     DATETIME     NULL,
  sort_pos     INT          NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS {p}commands (
  id          BIGINT AUTO_INCREMENT PRIMARY KEY,
  device_id   INT          NOT NULL,
  heater      TINYINT      NOT NULL DEFAULT 0,
  action      VARCHAR(16)  NOT NULL,
  value       INT          NOT NULL DEFAULT 0,
  status      ENUM('queued','sent','done','error','expired') NOT NULL DEFAULT 'queued',
  result      VARCHAR(200) NOT NULL DEFAULT '',
  created_at  DATETIME     NOT NULL,
  sent_at     DATETIME     NULL,
  done_at     DATETIME     NULL,
  INDEX idx_dev_status (device_id, status),
  CONSTRAINT {p}fk_cmd_dev FOREIGN KEY (device_id) REFERENCES {p}devices(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS {p}logs (
  id         BIGINT AUTO_INCREMENT PRIMARY KEY,
  device_id  INT          NOT NULL,
  ts         DATETIME     NOT NULL,
  msg        VARCHAR(240) NOT NULL,
  INDEX idx_dev (device_id, id),
  CONSTRAINT {p}fk_log_dev FOREIGN KEY (device_id) REFERENCES {p}devices(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS {p}readings (
  id         BIGINT AUTO_INCREMENT PRIMARY KEY,
  device_id  INT          NOT NULL,
  ts         DATETIME     NOT NULL,
  kind       VARCHAR(16)  NOT NULL,
  heater     TINYINT      NOT NULL DEFAULT 0,
  value      DECIMAL(7,1) NOT NULL,
  INDEX idx_dev_ts (device_id, ts),
  CONSTRAINT {p}fk_read_dev FOREIGN KEY (device_id) REFERENCES {p}devices(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS {p}login_fails (
  id  BIGINT AUTO_INCREMENT PRIMARY KEY,
  ip  VARCHAR(45) NOT NULL,
  ts  DATETIME    NOT NULL,
  INDEX idx_ip_ts (ip, ts)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Firmware releases for the controllers, one image per version and language (storage/firmware/<version>-<lang>.bin).
CREATE TABLE IF NOT EXISTS {p}firmware (
  version     INT          NOT NULL,
  lang        CHAR(2)      NOT NULL DEFAULT 'pl',
  size        INT          NOT NULL,
  md5         CHAR(32)     NOT NULL,
  filename    VARCHAR(120) NOT NULL DEFAULT '',
  created_at  DATETIME     NOT NULL,
  PRIMARY KEY (version, lang)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Periods in which a heater was working (state on or starting), the open one has no ended_at and is extended while it runs.
CREATE TABLE IF NOT EXISTS {p}runs (
  id          BIGINT AUTO_INCREMENT PRIMARY KEY,
  device_id   INT      NOT NULL,
  heater      TINYINT  NOT NULL DEFAULT 0,
  started_at  DATETIME NOT NULL,
  last_seen   DATETIME NOT NULL,
  ended_at    DATETIME NULL,
  INDEX idx_dev_start (device_id, heater, started_at),
  CONSTRAINT {p}fk_run_dev FOREIGN KEY (device_id) REFERENCES {p}devices(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Operating mode and power level while a heater works. Only manual and smart mode are recorded, the own auto mode of the heater has no level.
CREATE TABLE IF NOT EXISTS {p}steps (
  id          BIGINT AUTO_INCREMENT PRIMARY KEY,
  device_id   INT         NOT NULL,
  heater      TINYINT     NOT NULL DEFAULT 0,
  started_at  DATETIME    NOT NULL,
  last_seen   DATETIME    NOT NULL,
  ended_at    DATETIME    NULL,
  mode        VARCHAR(8)  NOT NULL,
  level       TINYINT     NOT NULL,
  INDEX idx_dev_start (device_id, heater, started_at),
  CONSTRAINT {p}fk_step_dev FOREIGN KEY (device_id) REFERENCES {p}devices(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
