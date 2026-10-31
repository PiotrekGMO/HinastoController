<?php
declare(strict_types=1);

namespace Hinasto\Services;

use Hinasto\Core\Config;
use Hinasto\Core\Db;
use Throwable;

/** Keeps the tables small: old readings, log lines, mode records and commands are deleted (7 days by default). */
final class Maintenance
{
    private const EVERY_S = 6 * 3600;
    private const BATCH = 5000;

    /** Called from every sync, does its work at most every few hours (the time of the last run is the time of a marker file). */
    public static function runIfDue(): void
    {
        $marker = ROOT . '/storage/last_purge';
        if (is_file($marker) && time() - (int)filemtime($marker) < self::EVERY_S) {
            return;
        }
        @touch($marker);  // first, so that syncs that arrive meanwhile do not start the same job
        $days = max(1, (int)Config::get('retention_days', 7));
        // working periods (table runs) stay: a few rows a day, and they carry the total work time
        $jobs = [
            'DELETE FROM {p}readings WHERE ts < NOW() - INTERVAL ? DAY LIMIT ' . self::BATCH,
            'DELETE FROM {p}logs WHERE ts < NOW() - INTERVAL ? DAY LIMIT ' . self::BATCH,
            'DELETE FROM {p}steps WHERE ended_at IS NOT NULL AND ended_at < NOW() - INTERVAL ? DAY LIMIT ' . self::BATCH,
            'DELETE FROM {p}commands WHERE created_at < NOW() - INTERVAL ? DAY LIMIT ' . self::BATCH,
            'DELETE FROM {p}login_fails WHERE ts < NOW() - INTERVAL ? DAY LIMIT ' . self::BATCH,
        ];
        foreach ($jobs as $i => $sql) {
            try {
                for ($n = 0; $n < 40; $n++) {  // in batches: never one long lock on a big table
                    if (Db::exec($sql, [$i === 4 ? 1 : $days]) < self::BATCH) {
                        break;
                    }
                }
            } catch (Throwable $e) {
                error_log('hinasto: purge failed: ' . $e->getMessage());
            }
        }
    }
}