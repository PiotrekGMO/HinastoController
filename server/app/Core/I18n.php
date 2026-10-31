<?php
declare(strict_types=1);

namespace Hinasto\Core;

/**
 * Translations. The language is chosen once, in the installer, and stored in storage/config.php ('lang');
 * there is no switcher in the panel. Texts live in app/Lang/<code>.php as flat key => string maps with {name}
 * placeholders. English is the fallback for a missing key, then the key itself.
 * Keys starting with "js." are sent to the browser (see View) and used by the panel scripts.
 */
final class I18n
{
    /** code => name of the language written in that language (shown in the installer) */
    public const LANGS = ['pl' => 'Polski', 'en' => 'English', 'de' => 'Deutsch', 'cs' => 'Čeština', 'sk' => 'Slovenčina'];
    private const LOCALES = ['pl' => 'pl-PL', 'en' => 'en-GB', 'de' => 'de-DE', 'cs' => 'cs-CZ', 'sk' => 'sk-SK'];
    public const DEFAULT = 'pl';  // installs made before the language was chosen at setup were Polish

    private static string $lang = self::DEFAULT;
    /** @var array<string,string> */
    private static array $msgs = [];
    /** @var array<string,string> */
    private static array $fallback = [];

    public static function valid(string $lang): bool
    {
        return isset(self::LANGS[$lang]);
    }

    public static function use(string $lang): void
    {
        self::$lang = self::valid($lang) ? $lang : self::DEFAULT;
        self::$msgs = self::load(self::$lang);
        self::$fallback = self::$lang === 'en' ? [] : self::load('en');
    }

    public static function lang(): string
    {
        return self::$lang;
    }

    /** Time zone suggested for the language in the installer. */
    public static function defaultTimezone(): string
    {
        return ['pl' => 'Europe/Warsaw', 'en' => 'Europe/London', 'de' => 'Europe/Berlin', 'cs' => 'Europe/Prague', 'sk' => 'Europe/Bratislava'][self::$lang];
    }

    /** BCP 47 tag for Intl / toLocale*String, e.g. "de-DE" */
    public static function locale(): string
    {
        return self::LOCALES[self::$lang];
    }

    /** Language from an Accept-Language header, for the installer before anything is chosen. */
    public static function detect(string $header): string
    {
        foreach (explode(',', $header) as $part) {
            $code = strtolower(substr(trim($part), 0, 2));
            if (self::valid($code)) {
                return $code;
            }
        }
        return 'en';
    }

    /** @param array<string,scalar> $params */
    public static function t(string $key, array $params = []): string
    {
        $text = self::$msgs[$key] ?? self::$fallback[$key] ?? $key;
        if ($params) {
            $map = [];
            foreach ($params as $name => $value) {
                $map['{' . $name . '}'] = (string)$value;
            }
            $text = strtr($text, $map);
        }
        return $text;
    }

    /** The strings the browser scripts need, plus the locale. */
    public static function forScripts(): array
    {
        $out = ['_locale' => self::locale()];
        foreach (self::$fallback + self::$msgs as $key => $_) {
            if (str_starts_with((string)$key, 'js.')) {
                $out[$key] = self::$msgs[$key] ?? self::$fallback[$key];
            }
        }
        return $out;
    }

    /** @return array<string,string> */
    private static function load(string $lang): array
    {
        $file = ROOT . '/app/Lang/' . $lang . '.php';
        return is_file($file) ? (array)require $file : [];
    }
}
