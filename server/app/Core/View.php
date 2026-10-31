<?php
declare(strict_types=1);

namespace Hinasto\Core;

/** Renders app/Views/<name>.php inside app/Views/layout.php. Views are HTML with <?= $e($x) ?>, <?= $t('key') ?> and <?= $th('key') ?> only. */
final class View
{
    private static string $base = '';

    /** URL prefix of the app (see Request::$base); views write links as <?= $e($base) ?>/path. */
    public static function setBase(string $base): void
    {
        self::$base = $base;
    }

    /** The newest change time of the files in public/assets: the version of the scripts and styles the page was built for. */
    public static function assetVersion(): string
    {
        $assets = glob(ROOT . '/public/assets/*') ?: [];
        return (string)max(array_map('filemtime', $assets) ?: [0]);
    }

    public static function render(string $view, array $data = []): string
    {
        $data += ['assetVersion' => self::assetVersion(), 'bodyClass' => '', 'script' => '', 'base' => self::$base, 'serverVersion' => Version::text(),
            'lang' => I18n::lang(), 'scriptStrings' => (string)json_encode(I18n::forScripts(), JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_HEX_TAG | JSON_HEX_AMP)];
        $content = self::capture($view, $data);
        return self::capture('layout', $data + ['content' => $content]);
    }

    private static function capture(string $name, array $data): string
    {
        $e = static fn(mixed $v): string => htmlspecialchars((string)$v, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
        $t = static fn(string $key, array $p = []): string => $e(I18n::t($key, $p));  // plain text, escaped
        // the translation itself may hold markup (<code>, <strong>, <a>); the parameters are escaped
        $th = static fn(string $key, array $p = []): string => I18n::t($key, array_map($e, $p));
        extract($data, EXTR_SKIP);
        ob_start();
        require ROOT . '/app/Views/' . $name . '.php';
        return (string)ob_get_clean();
    }
}
