<?php
declare(strict_types=1);

namespace Hinasto\Repositories;

use Hinasto\Core\Db;
use PDOException;

/** When a heater was working: one row per period, written from the state the controller reports. */
final class RunRepository
{
    private const GAP_S = 300;  // no news from the controller for this long ends a period at the last time it was seen working

    /** Called with the heaters of every accepted sync. Installs from before this existed get the table on first use. */
    public function observe(int $deviceId, array $heaters): void
    {
        try {
            $this->update($deviceId, $heaters);
        } catch (PDOException) {
            $this->ensureTable();
            $this->update($deviceId, $heaters);
        }
    }

    private function update(int $deviceId, array $heaters): void
    {
        foreach ($heaters as $h) {
            $i = (int)($h['i'] ?? 0);
            $run = (string)($h['run'] ?? 'unknown');
            $working = in_array($run, ['on', 'starting'], true);
            $open = Db::one(
                'SELECT id, UNIX_TIMESTAMP() - UNIX_TIMESTAMP(last_seen) AS silent FROM {p}runs
                 WHERE device_id = ? AND heater = ? AND ended_at IS NULL ORDER BY id DESC LIMIT 1',
                [$deviceId, $i]
            );
            if ($open !== null && (int)$open['silent'] > self::GAP_S) {  // the controller was away: that period ended when it was last seen
                Db::exec('UPDATE {p}runs SET ended_at = GREATEST(started_at, last_seen) WHERE id = ?', [$open['id']]);
                $open = null;
            }
            if ($working) {
                if ($open !== null) {
                    Db::exec('UPDATE {p}runs SET last_seen = NOW() WHERE id = ?', [$open['id']]);
                } else {
                    Db::exec('INSERT INTO {p}runs (device_id, heater, started_at, last_seen) VALUES (?, ?, NOW(), NOW())', [$deviceId, $i]);
                }
            } elseif ($open !== null && $run !== 'unknown') {  // "unknown" means no answer from the heater: neither start nor end
                Db::exec('UPDATE {p}runs SET ended_at = NOW() WHERE id = ?', [$open['id']]);
            }
        }
    }

    public function ensureTable(): void
    {
        foreach (explode(';', (string)preg_replace('/^\s*--.*$/m', '', (string)file_get_contents(ROOT . '/database/schema.sql'))) as $statement) {
            if (str_contains($statement, '{p}runs')) {
                Db::exec(trim($statement));
            }
        }
    }

    /** @return array<int, array{0:int,1:int}> [start, end] as unix times, the periods that touch [from, to] */
    public function intervals(int $deviceId, int $heater, int $from, int $to): array
    {
        $sql = 'SELECT UNIX_TIMESTAMP(started_at) AS s, UNIX_TIMESTAMP(COALESCE(ended_at, last_seen)) AS e FROM {p}runs
                WHERE device_id = ? AND heater = ? AND UNIX_TIMESTAMP(COALESCE(ended_at, last_seen)) >= ? AND UNIX_TIMESTAMP(started_at) <= ? ORDER BY started_at';
        try {
            $rows = Db::all($sql, [$deviceId, $heater, $from, $to]);
        } catch (PDOException) {
            $this->ensureTable();
            $rows = Db::all($sql, [$deviceId, $heater, $from, $to]);
        }
        return array_map(static fn(array $r): array => [(int)$r['s'], (int)$r['e']], $rows);
    }

    /** Seconds of work today (since $midnight), in the last 24 hours and in total, and since when the records go. */
    public function totals(int $deviceId, int $heater, int $now, int $midnight): array
    {
        $overlap = static fn(int $from): string => 'COALESCE(SUM(GREATEST(0, LEAST(UNIX_TIMESTAMP(COALESCE(ended_at, last_seen)), ' . $now . ') - GREATEST(UNIX_TIMESTAMP(started_at), ' . $from . '))), 0)';
        $sql = 'SELECT ' . $overlap($midnight) . ' AS day, ' . $overlap($now - 86400) . ' AS h24, ' . $overlap(0) . ' AS total, UNIX_TIMESTAMP(MIN(started_at)) AS since
                FROM {p}runs WHERE device_id = ? AND heater = ?';
        try {
            $r = Db::one($sql, [$deviceId, $heater]);
        } catch (PDOException) {
            $this->ensureTable();
            $r = Db::one($sql, [$deviceId, $heater]);
        }
        return ['day' => (int)$r['day'], 'h24' => (int)$r['h24'], 'total' => (int)$r['total'], 'since' => $r['since'] === null ? null : (int)$r['since']];
    }
}