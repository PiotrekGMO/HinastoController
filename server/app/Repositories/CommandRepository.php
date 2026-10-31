<?php
declare(strict_types=1);

namespace Hinasto\Repositories;

use Hinasto\Core\Config;
use Hinasto\Core\Db;
use Hinasto\Core\Text;

final class CommandRepository
{
    public function expireOld(): void
    {
        Db::exec(
            "UPDATE {p}commands SET status = 'expired', result = ?, done_at = NOW()
             WHERE status IN ('queued', 'sent') AND created_at < NOW() - INTERVAL ? SECOND",
            [t('cmd.expired'), (int)Config::get('command_ttl_seconds', 120)]
        );
    }

    public function queue(int $deviceId, int $heater, string $action, int $value): int
    {
        Db::exec(
            "INSERT INTO {p}commands (device_id, heater, action, value, status, created_at) VALUES (?, ?, ?, ?, 'queued', NOW())",
            [$deviceId, $heater, $action, $value]
        );
        return Db::lastId();
    }

    /**
     * Queues a command unless one is still in flight for the controller (power off always goes in). The check and the
     * insert run in one transaction that holds the controller's row, so two panel tabs cannot both pass the check.
     * @return int|null the id, null when another command is still running
     */
    public function queueExclusive(int $deviceId, int $heater, string $action, int $value): ?int
    {
        return Db::transaction(function () use ($deviceId, $heater, $action, $value): ?int {
            Db::value('SELECT id FROM {p}devices WHERE id = ? FOR UPDATE', [$deviceId]);
            if ($action !== 'off' && $this->activeCount($deviceId) >= 1) {
                return null;
            }
            return $this->queue($deviceId, $heater, $action, $value);
        });
    }

    public function activeCount(int $deviceId): int
    {
        return (int)Db::value("SELECT COUNT(*) FROM {p}commands WHERE device_id = ? AND status IN ('queued', 'sent')", [$deviceId]);
    }

    /** Takes the queued commands for a controller (power off first) and marks them as sent. */
    public function takeQueued(int $deviceId, int $limit = 5): array
    {
        $rows = Db::all(
            "SELECT id, heater, action, value FROM {p}commands WHERE device_id = ? AND status = 'queued'
             ORDER BY (action = 'off') DESC, id LIMIT " . (int)$limit,
            [$deviceId]
        );
        // claim every row with a conditional update: two overlapping syncs of one controller never get the same command
        $rows = array_values(array_filter($rows, static fn(array $r): bool =>
            Db::exec("UPDATE {p}commands SET status = 'sent', sent_at = NOW() WHERE id = ? AND status = 'queued'", [$r['id']]) === 1));
        return array_map(static fn(array $r): array => [
            'id' => (int)$r['id'],
            'heater' => (int)$r['heater'],
            'action' => $r['action'],
            'value' => (int)$r['value'],
        ], $rows);
    }

    public function acknowledge(int $deviceId, int $commandId, bool $ok, string $message): void
    {
        Db::exec(
            "UPDATE {p}commands SET status = ?, result = ?, done_at = NOW()
             WHERE id = ? AND device_id = ? AND status IN ('queued', 'sent', 'expired')",
            [$ok ? 'done' : 'error', Text::cut($message, 200), $commandId, $deviceId]
        );
    }

    /** The target of the newest "smart" command with a temperature (0 = off does not count), null when there was none. */
    public function lastSmartTarget(int $deviceId): ?int
    {
        $v = Db::value("SELECT value FROM {p}commands WHERE device_id = ? AND action = 'smart' AND value > 0 AND status <> 'error' ORDER BY id DESC LIMIT 1", [$deviceId]);
        return $v === null ? null : (int)$v;
    }

    public function recent(int $deviceId, int $limit = 6): array
    {
        $rows = Db::all(
            "SELECT id, heater, action, value, status, result, TIMESTAMPDIFF(SECOND, COALESCE(done_at, created_at), NOW()) AS age FROM {p}commands
             WHERE device_id = ? AND created_at > NOW() - INTERVAL 10 MINUTE ORDER BY id DESC LIMIT " . (int)$limit,
            [$deviceId]
        );
        return array_map(static fn(array $c): array => [
            'id' => (int)$c['id'], 'heater' => (int)$c['heater'], 'action' => $c['action'],
            'value' => (int)$c['value'], 'status' => $c['status'], 'result' => $c['result'], 'age' => (int)$c['age'],
        ], $rows);
    }
}
