<?php
declare(strict_types=1);

namespace Hinasto\Services;

use Hinasto\Core\I18n;
use PDO;
use PDOException;
use RuntimeException;

/**
 * One-time installation: optionally creates the database and its user (MySQL root needed for that),
 * imports database/schema.sql, writes storage/config.php. Used by the /setup page and by tools/setup.php.
 */
final class Installer
{
    /**
     * @param array{db_host:string, db_name:string, db_user:string, db_pass:string, db_prefix?:string, root_user?:string, root_pass?:string,
     *              admin_password:string, enroll_key?:string, timezone?:string, lang?:string} $in
     * @return array{enroll_key:string}
     */
    public function install(array $in): array
    {
        $host = trim($in['db_host']) ?: 'localhost';
        $name = trim($in['db_name']);
        $user = trim($in['db_user']);
        $pass = $in['db_pass'];
        if (!preg_match('/^[A-Za-z0-9_]{1,64}$/', $name) || !preg_match('/^[A-Za-z0-9_]{1,32}$/', $user)) {
            throw new RuntimeException(t('install.names'));
        }
        if (strlen($in['admin_password']) < 10) {
            throw new RuntimeException(t('install.admin_short'));
        }
        if (($in['root_user'] ?? '') !== '') {
            $this->createDatabaseAndUser($host, $in['root_user'], $in['root_pass'] ?? '', $name, $user, $pass);
        }

        $prefix = trim($in['db_prefix'] ?? '');
        if (!preg_match('/^[A-Za-z0-9_]{0,20}$/', $prefix)) {
            throw new RuntimeException(t('install.prefix'));
        }

        $pdo = $this->connect($host, $name, $user, $pass);
        $this->importSchema($pdo, $prefix);

        $enroll = trim($in['enroll_key'] ?? '') ?: bin2hex(random_bytes(12));
        $config = [
            'db' => ['host' => $host, 'name' => $name, 'user' => $user, 'pass' => $pass, 'prefix' => $prefix],
            'timezone' => ($in['timezone'] ?? '') ?: I18n::defaultTimezone(),
            'lang' => I18n::valid($in['lang'] ?? '') ? $in['lang'] : I18n::lang(),
            'admin_hash' => password_hash($in['admin_password'], PASSWORD_DEFAULT),
            'enroll_key' => $enroll,
            'online_seconds' => 20,
            'command_ttl_seconds' => 120,
        ];
        $file = ROOT . '/storage/config.php';
        $php = "<?php\n// Written by the installer. Edit by hand only if you know why.\nreturn " . var_export($config, true) . ";\n";
        if (!is_dir(ROOT . '/storage') || !is_writable(ROOT . '/storage') || @file_put_contents($file, $php, LOCK_EX) === false) {
            throw new ConfigNotWritable($php, $enroll);  // the tables exist already; the file can be saved by hand
        }
        @chmod($file, 0640);
        return ['enroll_key' => $enroll];
    }

    /**
     * What the host must provide before the installer can finish: shown at the top of /setup.
     * @return array<int, array{label:string, ok:bool, hint:string}>
     */
    public function preflight(): array
    {
        $dir = ROOT . '/storage';
        if (is_dir($dir) && !is_writable($dir)) {
            @chmod($dir, 0775);  // works when PHP owns the folder; otherwise the deploy script sets the mode
            clearstatcache(true, $dir);
        }
        return [
            [
                'label' => t('install.pf_php', ['version' => PHP_VERSION]),
                'ok' => version_compare(PHP_VERSION, '8.1.0', '>='),
                'hint' => t('install.pf_php_hint'),
            ],
            [
                'label' => t('install.pf_pdo'),
                'ok' => extension_loaded('pdo_mysql'),
                'hint' => t('install.pf_pdo_hint'),
            ],
            [
                'label' => t('install.pf_storage'),
                'ok' => is_dir($dir) && is_writable($dir),
                'hint' => t('install.pf_storage_hint'),
            ],
        ];
    }

    private function connect(string $host, ?string $name, string $user, string $pass): PDO
    {
        $dsn = "mysql:host=$host;charset=utf8mb4" . ($name !== null ? ";dbname=$name" : '');
        try {
            return new PDO($dsn, $user, $pass, [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);
        } catch (PDOException $e) {
            throw new RuntimeException(t('install.db_connect', ['error' => $e->getMessage()]));
        }
    }

    private function createDatabaseAndUser(string $host, string $rootUser, string $rootPass, string $name, string $user, string $pass): void
    {
        $pdo = $this->connect($host, null, $rootUser, $rootPass);
        $grantHost = in_array($host, ['localhost', '127.0.0.1', '::1'], true) ? 'localhost' : '%';
        $pdo->exec("CREATE DATABASE IF NOT EXISTS `$name` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci");
        $st = $pdo->prepare("CREATE USER IF NOT EXISTS '$user'@'$grantHost' IDENTIFIED BY ?");
        $st->execute([$pass]);
        $pdo->exec("GRANT ALL PRIVILEGES ON `$name`.* TO '$user'@'$grantHost'");
        $pdo->exec('FLUSH PRIVILEGES');
    }

    private function importSchema(PDO $pdo, string $prefix): void
    {
        $sql = str_replace('{p}', $prefix, (string)preg_replace('/^\s*--.*$/m', '', (string)file_get_contents(ROOT . '/database/schema.sql')));
        $sql = preg_replace('/^\s*--.*$/m', '', $sql);
        foreach (array_filter(array_map('trim', explode(';', (string)$sql))) as $statement) {
            $pdo->exec($statement);
        }
    }
}
