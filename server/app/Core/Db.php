<?php
declare(strict_types=1);

namespace Hinasto\Core;

use PDO;
use PDOStatement;

/** Thin PDO wrapper. SQL lives in the repositories, nowhere else. */
final class Db
{
    private static ?PDO $pdo = null;

    private static function pdo(): PDO
    {
        if (self::$pdo === null) {
            $c = Config::get('db');
            self::$pdo = new PDO(
                "mysql:host={$c['host']};dbname={$c['name']};charset=utf8mb4",
                $c['user'],
                $c['pass'],
                [
                    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
                    PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
                    PDO::ATTR_EMULATE_PREPARES => false,
                ]
            );
        }
        return self::$pdo;
    }

    /** Tables are written as {p}name in SQL; the prefix comes from the setup. */
    private static function run(string $sql, array $params): PDOStatement
    {
        $st = self::pdo()->prepare(str_replace('{p}', (string)Config::get('db.prefix', ''), $sql));
        $st->execute($params);
        return $st;
    }

    /** @return int affected rows */
    public static function exec(string $sql, array $params = []): int
    {
        return self::run($sql, $params)->rowCount();
    }

    /** @return array<int, array<string, mixed>> */
    public static function all(string $sql, array $params = []): array
    {
        return self::run($sql, $params)->fetchAll();
    }

    /** @return array<string, mixed>|null */
    public static function one(string $sql, array $params = []): ?array
    {
        $row = self::run($sql, $params)->fetch();
        return $row === false ? null : $row;
    }

    public static function value(string $sql, array $params = []): mixed
    {
        $row = self::run($sql, $params)->fetch(PDO::FETCH_NUM);
        return $row === false ? null : $row[0];
    }

    public static function lastId(): int
    {
        return (int)self::pdo()->lastInsertId();
    }

    /**
     * Runs $work in a transaction: committed when it returns, rolled back when it throws.
     * @template T
     * @param callable(): T $work
     * @return T
     */
    public static function transaction(callable $work): mixed
    {
        $pdo = self::pdo();
        $pdo->beginTransaction();
        try {
            $result = $work();
            $pdo->commit();
            return $result;
        } catch (\Throwable $e) {
            if ($pdo->inTransaction()) {
                $pdo->rollBack();
            }
            throw $e;
        }
    }

    /** One prepared statement run for many rows. */
    public static function insertMany(string $sql, array $rows): void
    {
        $st = self::pdo()->prepare(str_replace('{p}', (string)Config::get('db.prefix', ''), $sql));
        foreach ($rows as $params) {
            $st->execute($params);
        }
    }
}
