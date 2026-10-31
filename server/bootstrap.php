<?php
declare(strict_types=1);

use Hinasto\Core\Config;

define('ROOT', __DIR__);

// PHP warnings and notices must never end up in a response (paths, broken JSON); they go to the error log
ini_set('display_errors', '0');

// PSR-4 style autoloader: Hinasto\Core\Db  ->  app/Core/Db.php
spl_autoload_register(static function (string $class): void {
    $prefix = 'Hinasto\\';
    if (strncmp($class, $prefix, strlen($prefix)) !== 0) {
        return;
    }
    $file = ROOT . '/app/' . str_replace('\\', '/', substr($class, strlen($prefix))) . '.php';
    if (is_file($file)) {
        require $file;
    }
});

// storage/ survives deployments (the deploy script keeps it); config.php is missing before the first setup
if (is_file(ROOT . '/storage/config.php')) {
    Config::load(ROOT . '/storage/config.php');
}
date_default_timezone_set((string)Config::get('timezone', 'Europe/Warsaw'));

// the language is chosen once in the installer; before that (no config yet) the installer picks one itself
\Hinasto\Core\I18n::use((string)Config::get('lang', \Hinasto\Core\I18n::DEFAULT));

function t(string $key, array $params = []): string
{
    return \Hinasto\Core\I18n::t($key, $params);
}
