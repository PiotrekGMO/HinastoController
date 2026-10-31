<?php
declare(strict_types=1);

// Command line installer, same as the /setup page:  php tools/setup.php
if (PHP_SAPI !== 'cli') {
    exit("Run from the command line.\n");
}

require __DIR__ . '/../bootstrap.php';

use Hinasto\Core\I18n;
use Hinasto\Services\Installer;

if (is_file(ROOT . '/storage/config.php')) {
    exit("Already installed (storage/config.php exists). Delete it first to install again.\n");
}

function ask(string $label, string $default = ''): string
{
    echo $label . ($default !== '' ? " [$default]" : '') . ': ';
    $line = trim((string)fgets(STDIN));
    return $line !== '' ? $line : $default;
}

echo "Hinasto Controller, installation\n\n";
$lang = strtolower(ask('Language of the panel (' . implode('/', array_keys(I18n::LANGS)) . ')', 'en'));
I18n::use(I18n::valid($lang) ? $lang : 'en');
$in = [
    'lang' => I18n::lang(),
    'db_host' => ask('Database host', 'localhost'),
    'db_name' => ask('Database name'),
    'db_user' => ask('Database user'),
    'db_pass' => ask('Database password'),
    'db_prefix' => ask('Table prefix (shared database?)', 'hn_'),
    'root_user' => ask('MySQL admin account to create the database and user (empty = they already exist)'),
    'root_pass' => '',
    'admin_password' => ask('Panel admin password (min. 10 characters)'),
    'enroll_key' => ask('Enrollment key for controllers (empty = generate)'),
    'timezone' => ask('Time zone', I18n::defaultTimezone()),
];
if ($in['root_user'] !== '') {
    $in['root_pass'] = ask('Password of that MySQL account');
}

try {
    $result = (new Installer())->install($in);
} catch (RuntimeException $e) {
    fwrite(STDERR, "\nFailed: " . $e->getMessage() . "\n");
    exit(1);
}
echo "\nDone. storage/config.php written.\nEnrollment key for the controllers: {$result['enroll_key']}\n";
