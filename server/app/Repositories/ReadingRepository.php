<?php
declare(strict_types=1);

namespace Hinasto\Repositories;

use Hinasto\Core\Db;

final class ReadingRepository
{
    public const KINDS = ['env_t', 'env_h', 'chamber', 'volts'];

    /** Stores a set of readings, at most one set a minute per controller. @param array<int, array{0:string,1:int,2:float}> $rows kind, heater, value */
    public function addIfDue(int $deviceId, array $rows): void
    {
        $recent = (int)Db::value('SELECT COUNT(*) FROM {p}readings WHERE device_id = ? AND ts > NOW() - INTERVAL 60 SECOND', [$deviceId]);
        if ($recent > 0 || !$rows) {
            return;
        }
        Db::insertMany(
            'INSERT INTO {p}readings (device_id, ts, kind, heater, value) VALUES (?, NOW(), ?, ?, ?)',
            array_map(static fn(array $r): array => [$deviceId, $r[0], $r[1], $r[2]], $rows)
        );
    }

    /** @return array{min: ?float, avg: ?float, max: ?float} over the last $hours hours */
    public function stats(int $deviceId, string $kind, int $heater, int $hours): array
    {
        $r = Db::one(
            'SELECT MIN(value) AS lo, AVG(value) AS av, MAX(value) AS hi FROM {p}readings
             WHERE device_id = ? AND kind = ? AND heater = ? AND ts > NOW() - INTERVAL ? HOUR',
            [$deviceId, $kind, $heater, $hours]
        );
        return ['min' => $r['lo'] === null ? null : round((float)$r['lo'], 1), 'avg' => $r['av'] === null ? null : round((float)$r['av'], 1), 'max' => $r['hi'] === null ? null : round((float)$r['hi'], 1)];
    }

    /** About 300 points at most, each the mean of a time bucket (smooths the sensor noise): [[unix time, value], ...] */
    public function series(int $deviceId, string $kind, int $heater, int $hours): array
    {
        $bucket = max(60, (int)ceil($hours * 3600 / 300));
        $rows = Db::all(
            'SELECT FLOOR(UNIX_TIMESTAMP(ts) / ' . $bucket . ') * ' . $bucket . ' AS t, AVG(value) AS v FROM {p}readings
             WHERE device_id = ? AND kind = ? AND heater = ? AND ts > NOW() - INTERVAL ? HOUR GROUP BY t ORDER BY t',
            [$deviceId, $kind, $heater, $hours]
        );
        return array_map(static fn(array $r): array => [(int)$r['t'] + intdiv($bucket, 2), round((float)$r['v'], 2)], $rows);
    }
}
