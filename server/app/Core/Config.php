<?php
declare(strict_types=1);

namespace Hinasto\Core;

final class Config
{
    private static array $data = [];
    private static bool $loaded = false;

    public static function load(string $file): void
    {
        self::$data = require $file;
        self::$loaded = true;
    }

    /** False until the installer has written storage/config.php. */
    public static function isLoaded(): bool
    {
        return self::$loaded;
    }

    /** Dot notation: Config::get('db.host') */
    public static function get(string $key, mixed $default = null): mixed
    {
        $value = self::$data;
        foreach (explode('.', $key) as $part) {
            if (!is_array($value) || !array_key_exists($part, $value)) {
                return $default;
            }
            $value = $value[$part];
        }
        return $value;
    }
}
