<?php
declare(strict_types=1);

namespace Hinasto\Repositories;

use Hinasto\Core\Db;

final class LoginAttemptRepository
{
    /** What failures are counted by: the address, an IPv6 address by its /64 (one subscriber owns that whole range). */
    public static function key(string $ip): string
    {
        if (str_contains($ip, ':') && ($packed = @inet_pton($ip)) !== false && strlen($packed) === 16) {
            return substr(inet_ntop(substr($packed, 0, 8) . str_repeat("\0", 8)) ?: $ip, 0, 45);
        }
        return substr($ip, 0, 45);
    }

    public function recentFailures(string $ip): int
    {
        return (int)Db::value('SELECT COUNT(*) FROM {p}login_fails WHERE ip = ? AND ts > NOW() - INTERVAL 15 MINUTE', [$ip]);
    }

    public function addFailure(string $ip): void
    {
        Db::exec('INSERT INTO {p}login_fails (ip, ts) VALUES (?, NOW())', [$ip]);
    }

    public function clear(string $ip): void
    {
        Db::exec('DELETE FROM {p}login_fails WHERE ip = ?', [$ip]);
    }
}
