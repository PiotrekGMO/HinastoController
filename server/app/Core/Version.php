<?php
declare(strict_types=1);

namespace Hinasto\Core;

/** Version of the server application: the VERSION file (raised by hand per release) and the BUILD stamp the deploy adds. */
final class Version
{
    /** @return array{version: string, build: string} */
    public static function info(): array
    {
        static $info = null;
        if ($info === null) {
            $version = is_file(ROOT . '/VERSION') ? trim((string)file_get_contents(ROOT . '/VERSION')) : '';
            $build = is_file(ROOT . '/BUILD') ? trim((string)file_get_contents(ROOT . '/BUILD')) : '';
            $info = ['version' => $version !== '' ? $version : '?', 'build' => $build !== '' ? $build : t('layout.local')];
        }
        return $info;
    }

    public static function text(): string
    {
        $i = self::info();
        return 'v' . $i['version'] . ' · ' . $i['build'];
    }
}