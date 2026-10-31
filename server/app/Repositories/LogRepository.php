<?php
declare(strict_types=1);

namespace Hinasto\Repositories;

use Hinasto\Core\Db;
use Hinasto\Core\Text;

final class LogRepository
{
    private const KEEP = 500;

    /** @param string[] $messages */
    public function add(int $deviceId, array $messages): void
    {
        $rows = [];
        foreach ($messages as $m) {
            if ($m !== '') {
                $rows[] = [$deviceId, Text::cut($m, 240)];
            }
        }
        if ($rows) {
            Db::insertMany('INSERT INTO {p}logs (device_id, ts, msg) VALUES (?, NOW(), ?)', $rows);
        }
        if (random_int(1, 20) === 1) {
            $this->trim($deviceId);
        }
    }

    private function trim(int $deviceId): void
    {
        $cut = Db::value('SELECT id FROM {p}logs WHERE device_id = ? ORDER BY id DESC LIMIT 1 OFFSET ' . self::KEEP, [$deviceId]);
        if ($cut !== null) {
            Db::exec('DELETE FROM {p}logs WHERE device_id = ? AND id <= ?', [$deviceId, $cut]);
        }
    }

    /** Lines after $afterId, oldest first. */
    public function since(int $deviceId, int $afterId): array
    {
        $rows = Db::all(
            "SELECT id, DATE_FORMAT(ts, '%H:%i:%s') AS t, msg FROM {p}logs WHERE device_id = ? AND id > ? ORDER BY id DESC LIMIT 100",
            [$deviceId, $afterId]
        );
        return array_reverse($rows);
    }
}
