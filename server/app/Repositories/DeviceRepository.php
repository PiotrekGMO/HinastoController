<?php
declare(strict_types=1);

namespace Hinasto\Repositories;

use Hinasto\Core\Config;
use Hinasto\Core\Db;
use PDOException;

final class DeviceRepository
{
    private const WITH_AGO = '*, TIMESTAMPDIFF(SECOND, last_seen, NOW()) AS seen_ago, TIMESTAMPDIFF(SECOND, state_at, NOW()) AS state_ago';

    public function findByDeviceId(string $deviceId): ?array
    {
        return Db::one('SELECT ' . self::WITH_AGO . ' FROM {p}devices WHERE device_id = ?', [$deviceId]);
    }

    public function find(int $id): ?array
    {
        return Db::one('SELECT ' . self::WITH_AGO . ' FROM {p}devices WHERE id = ?', [$id]);
    }

    public function findApproved(int $id): ?array
    {
        return Db::one("SELECT " . self::WITH_AGO . " FROM {p}devices WHERE id = ? AND status = 'approved'", [$id]);
    }

    /** Controllers waiting for approval, all of them or only those that came from one IP address. */
    public function countPending(?string $ip = null): int
    {
        return $ip === null
            ? (int)Db::value("SELECT COUNT(*) FROM {p}devices WHERE status = 'pending'")
            : (int)Db::value("SELECT COUNT(*) FROM {p}devices WHERE status = 'pending' AND ip = ?", [$ip]);
    }

    /** A pending controller that has not been heard for an hour has given up: its place is freed. */
    public function dropStalePending(): void
    {
        Db::exec("DELETE FROM {p}devices WHERE status = 'pending' AND last_seen < NOW() - INTERVAL 1 HOUR");
    }

    public function createPending(string $deviceId, string $secretHash, string $fw, string $ip): void
    {
        Db::exec(
            "INSERT INTO {p}devices (device_id, secret_hash, status, fw, ip, first_seen, last_seen)
             VALUES (?, ?, 'pending', ?, ?, NOW(), NOW())",
            [$deviceId, $secretHash, $fw, $ip]
        );
    }

    public function touch(int $id, string $ip, string $fw): void
    {
        Db::exec('UPDATE {p}devices SET last_seen = NOW(), ip = ?, fw = IF(? = \'\', fw, ?) WHERE id = ?', [$ip, $fw, $fw, $id]);
    }

    public function saveState(int $id, array $state): void
    {
        Db::exec('UPDATE {p}devices SET state_json = ?, state_at = NOW() WHERE id = ?', [json_encode($state, JSON_UNESCAPED_UNICODE | JSON_INVALID_UTF8_SUBSTITUTE | JSON_PARTIAL_OUTPUT_ON_ERROR), $id]);
    }

    /** Approved, pending and rejected controllers, for the panel. */
    public function listForPanel(): array
    {
        $sql = "SELECT " . self::WITH_AGO . " FROM {p}devices WHERE status IN ('approved', 'pending', 'rejected') ORDER BY sort_pos, name, id";
        try {
            return Db::all($sql);
        } catch (PDOException) {  // an install from before the order existed: add the column once
            $this->ensureSortColumn();
            return Db::all($sql);
        }
    }

    private function ensureSortColumn(): void
    {
        $table = (string)Config::get('db.prefix', '') . 'devices';
        $has = Db::value("SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = ? AND COLUMN_NAME = 'sort_pos'", [$table]);
        if ((int)$has === 0) {
            Db::exec('ALTER TABLE {p}devices ADD COLUMN sort_pos INT NOT NULL DEFAULT 0');
        }
    }

    /** One step up (-1) or down (+1) among the approved controllers; positions are renumbered each time. */
    public function move(int $id, int $dir): void
    {
        $this->ensureSortColumn();
        $ids = array_map('intval', array_column(Db::all("SELECT id FROM {p}devices WHERE status = 'approved' ORDER BY sort_pos, name, id"), 'id'));
        $i = array_search($id, $ids, true);
        $j = $i === false ? false : $i + ($dir < 0 ? -1 : 1);
        if ($i !== false && $j >= 0 && $j < count($ids)) {
            [$ids[$i], $ids[$j]] = [$ids[$j], $ids[$i]];
        }
        foreach ($ids as $pos => $deviceId) {
            Db::exec('UPDATE {p}devices SET sort_pos = ? WHERE id = ?', [$pos, $deviceId]);
        }
    }

    public function approve(int $id, string $name): void
    {
        Db::exec("UPDATE {p}devices SET status = 'approved', approved_at = NOW(), name = ? WHERE id = ?", [$name, $id]);
    }

    public function reject(int $id): void
    {
        Db::exec("UPDATE {p}devices SET status = 'rejected' WHERE id = ?", [$id]);
    }

    public function rename(int $id, string $name): void
    {
        Db::exec('UPDATE {p}devices SET name = ? WHERE id = ?', [$name, $id]);
    }

    public function delete(int $id): void
    {
        Db::exec('DELETE FROM {p}devices WHERE id = ?', [$id]);
    }
}
