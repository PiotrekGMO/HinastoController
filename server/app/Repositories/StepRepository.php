<?php
declare(strict_types=1);

namespace Hinasto\Repositories;

use Hinasto\Core\Db;
use PDOException;

/** Mode and power level of a working heater, as periods: a new row only when the mode or the level changes. */
final class StepRepository
{
    private const GAP_S = 300;

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
            $smart = is_array($h['smart'] ?? null) && !empty($h['smart']['on']);
            $mode = $smart ? 'smart' : (!empty($h['manual']) ? 'manual' : null);  // the own auto mode of the heater has no level: nothing to record
            $level = isset($h['level']) ? (int)$h['level'] : 0;
            $want = ($run === 'on' && $mode !== null && $level >= 1 && $level <= 6) ? [$mode, $level] : null;

            $open = Db::one(
                'SELECT id, mode, level, UNIX_TIMESTAMP() - UNIX_TIMESTAMP(last_seen) AS silent FROM {p}steps
                 WHERE device_id = ? AND heater = ? AND ended_at IS NULL ORDER BY id DESC LIMIT 1',
                [$deviceId, $i]
            );
            if ($open !== null && (int)$open['silent'] > self::GAP_S) {
                Db::exec('UPDATE {p}steps SET ended_at = GREATEST(started_at, last_seen) WHERE id = ?', [$open['id']]);
                $open = null;
            }
            if ($want !== null) {
                if ($open !== null && $open['mode'] === $want[0] && (int)$open['level'] === $want[1]) {
                    Db::exec('UPDATE {p}steps SET last_seen = NOW() WHERE id = ?', [$open['id']]);
                    continue;
                }
                if ($open !== null) {
                    Db::exec('UPDATE {p}steps SET ended_at = NOW() WHERE id = ?', [$open['id']]);
                }
                Db::exec(
                    'INSERT INTO {p}steps (device_id, heater, started_at, last_seen, mode, level) VALUES (?, ?, NOW(), NOW(), ?, ?)',
                    [$deviceId, $i, $want[0], $want[1]]
                );
            } elseif ($open !== null && $run !== 'unknown') {
                Db::exec('UPDATE {p}steps SET ended_at = NOW() WHERE id = ?', [$open['id']]);
            }
        }
    }

    public function ensureTable(): void
    {
        $sql = (string)preg_replace('/^\s*--.*$/m', '', (string)file_get_contents(ROOT . '/database/schema.sql'));
        foreach (explode(';', $sql) as $statement) {
            if (str_contains($statement, '{p}steps')) {
                Db::exec(trim($statement));
            }
        }
    }

    /** @return array<int, array{0:int,1:int,2:string,3:int}> start, end, mode, level of the periods that touch [from, to] */
    public function intervals(int $deviceId, int $heater, int $from, int $to): array
    {
        $sql = 'SELECT UNIX_TIMESTAMP(started_at) AS s, UNIX_TIMESTAMP(COALESCE(ended_at, last_seen)) AS e, mode, level FROM {p}steps
                WHERE device_id = ? AND heater = ? AND UNIX_TIMESTAMP(COALESCE(ended_at, last_seen)) >= ? AND UNIX_TIMESTAMP(started_at) <= ? ORDER BY started_at';
        try {
            $rows = Db::all($sql, [$deviceId, $heater, $from, $to]);
        } catch (PDOException) {
            $this->ensureTable();
            $rows = Db::all($sql, [$deviceId, $heater, $from, $to]);
        }
        return array_map(static fn(array $r): array => [(int)$r['s'], (int)$r['e'], (string)$r['mode'], (int)$r['level']], $rows);
    }
}