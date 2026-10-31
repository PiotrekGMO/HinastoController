<?php
declare(strict_types=1);

namespace Hinasto\Repositories;

use Hinasto\Core\Config;
use Hinasto\Core\Db;
use Hinasto\Core\I18n;
use PDOException;

/**
 * Firmware releases: the table row says what exists, the image is storage/firmware/<version>-<lang>.bin.
 * Every language has its own builds and its own version line: a controller only ever updates to its own language.
 */
final class FirmwareRepository
{
    private const KEEP = 5;  // releases kept per language

    public static function dir(): string
    {
        return ROOT . '/storage/firmware';
    }

    public static function path(int $version, string $lang): string
    {
        $file = self::dir() . '/' . $version . '-' . $lang . '.bin';
        $legacy = self::dir() . '/' . $version . '.bin';  // before languages existed every image was Polish
        return !is_file($file) && $lang === 'pl' && is_file($legacy) ? $legacy : $file;
    }

    /**
     * A plain file at public/api/v1/firmware-<lang>: the web server in front of PHP answers dynamic requests in chunks
     * (no Content-Length) and some firmware versions refuse such an answer, a static file has its length.
     * It is the newest image of that language, for anybody who asks the URL the controllers use; the image holds no secrets.
     * The file "firmware" without a language is what controllers from before languages existed ask for: the newest
     * Polish image (those builds were all Polish).
     */
    public static function mirror(int $version, string $lang): bool
    {
        $dir = ROOT . '/public/api/v1';
        $src = self::path($version, $lang);
        if (!is_dir($dir) || !is_writable($dir) || !is_file($src)) {
            error_log('hinasto: no static firmware copy (public/api/v1 is not writable)');
            return false;
        }
        $ok = self::publishCopy($src, $dir . '/firmware-' . $lang);
        if ($ok && $lang === 'pl') {
            $ok = self::publishCopy($src, $dir . '/firmware');
        }
        return $ok;
    }

    /** Copies through a temporary file of its own (a unique name: parallel requests must not mix two images) and renames it into place. */
    private static function publishCopy(string $src, string $dest): bool
    {
        $tmp = dirname($dest) . '/.fw-' . bin2hex(random_bytes(6)) . '.tmp';
        if (!@copy($src, $tmp) || !@rename($tmp, $dest)) {
            @unlink($tmp);
            return false;
        }
        return true;
    }

    /** A deploy replaces public/ and with it the static copies: bring them back from storage/ when one is missing or is not the release (size and md5). */
    public static function ensureMirror(array $release): void
    {
        $targets = [ROOT . '/public/api/v1/firmware-' . $release['lang']];
        if ($release['lang'] === 'pl') {
            $targets[] = ROOT . '/public/api/v1/firmware';
        }
        foreach ($targets as $file) {
            if (!is_file($file) || filesize($file) !== (int)$release['size'] || md5_file($file) !== $release['md5']) {
                self::mirror((int)$release['version'], (string)$release['lang']);
                return;
            }
        }
    }
    /** Installs made before releases (or languages) existed lack the table (or the column): create or extend it. */
    public function ensureTable(): void
    {
        $sql = (string)preg_replace('/^\s*--.*$/m', '', (string)file_get_contents(ROOT . '/database/schema.sql'));
        foreach (explode(';', $sql) as $statement) {
            if (str_contains($statement, '{p}firmware')) {
                Db::exec(trim($statement));
            }
        }
        $table = (string)Config::get('db.prefix', '') . 'firmware';
        $has = Db::value("SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = ? AND COLUMN_NAME = 'lang'", [$table]);
        if ((int)$has === 0) {
            Db::exec("ALTER TABLE {p}firmware ADD COLUMN lang CHAR(2) NOT NULL DEFAULT 'pl' AFTER version, DROP PRIMARY KEY, ADD PRIMARY KEY (version, lang)");
        }
    }

    /** Runs a read; a missing table or column (an install from before) is created once and the read repeated, a second failure gives $default. */
    private function guard(callable $read, mixed $default): mixed
    {
        try {
            return $read();
        } catch (PDOException) {
            try {
                $this->ensureTable();
                return $read();
            } catch (PDOException) {
                return $default;
            }
        }
    }

    /** Newest release of a language, or null (also when the table does not exist yet: the sync must never fail because of it). */
    public function latest(string $lang): ?array
    {
        return $this->guard(fn() => Db::one('SELECT * FROM {p}firmware WHERE lang = ? ORDER BY version DESC LIMIT 1', [$lang]), null);
    }

    /** Newest release of every language that has one, keyed by language. */
    public function latestByLang(): array
    {
        $out = [];
        foreach ($this->recent() as $row) {  // newest first
            $out[$row['lang']] ??= $row;
        }
        return $out;
    }

    public function find(int $version, string $lang): ?array
    {
        return $this->guard(fn() => Db::one('SELECT * FROM {p}firmware WHERE version = ? AND lang = ?', [$version, $lang]), null);
    }

    public function recent(): array
    {
        return $this->guard(fn() => Db::all('SELECT * FROM {p}firmware ORDER BY version DESC, lang'), []);
    }

    public function save(int $version, string $lang, int $size, string $md5, string $filename): void
    {
        Db::exec(
            'REPLACE INTO {p}firmware (version, lang, size, md5, filename, created_at) VALUES (?, ?, ?, ?, ?, NOW())',
            [$version, $lang, $size, $md5, substr($filename, 0, 120)]
        );
        $old = Db::all('SELECT version FROM {p}firmware WHERE lang = ? ORDER BY version DESC LIMIT 100 OFFSET ' . self::KEEP, [$lang]);
        foreach ($old as $row) {
            Db::exec('DELETE FROM {p}firmware WHERE version = ? AND lang = ?', [$row['version'], $lang]);
            @unlink(self::path((int)$row['version'], $lang));
        }
    }

    /**
     * The language of the build to offer to a controller: its own, "pl" when it reports none (builds from before
     * languages existed were all Polish), null when it reports a language this server has no builds for.
     */
    public static function offerLang(?string $reported): ?string
    {
        if ($reported === null || $reported === '') {
            return 'pl';
        }
        return I18n::valid($reported) ? $reported : null;
    }

    /** Language a controller reports (2 letters), "pl" for firmware from before languages existed (display only). */
    public static function normalizeLang(string $lang): string
    {
        $lang = strtolower(substr($lang, 0, 2));
        return I18n::valid($lang) ? $lang : 'pl';
    }
}
