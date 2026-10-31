<?php
declare(strict_types=1);

// Front controller: everything that is not a static file in public/ ends up here.
if (PHP_SAPI === 'cli-server') {  // local `php -S`: let the built-in server serve real files
    $file = __DIR__ . (parse_url((string)$_SERVER['REQUEST_URI'], PHP_URL_PATH) ?: '/');
    if (is_file($file)) {
        return false;
    }
}

require __DIR__ . '/../bootstrap.php';

use Hinasto\Core\Auth;
use Hinasto\Core\Config;
use Hinasto\Core\HttpException;
use Hinasto\Core\I18n;
use Hinasto\Core\Request;
use Hinasto\Core\Response;
use Hinasto\Core\Router;
use Hinasto\Core\View;

$request = Request::fromGlobals();
View::setBase($request->base);
Auth::setBase($request->base);
$isApi = str_starts_with($request->path, '/api/');
if (!Config::isLoaded()) {  // installer: the language is not chosen yet, take the one from the page (?lang= / hidden field) or the browser
    $wanted = $request->post('lang', $request->query('lang'));
    I18n::use(I18n::valid($wanted) ? $wanted : I18n::detect($request->header('Accept-Language')));
}

try {
    $routes = require ROOT . '/config/' . (Config::isLoaded() ? 'routes.php' : 'routes.setup.php');
    (new Router($routes))->dispatch($request)->send();
} catch (HttpException $e) {
    ($isApi ? Response::json(['error' => $e->getMessage()], $e->status) : Response::html('<h1>' . $e->status . '</h1>', $e->status))->send();
} catch (Throwable $e) {
    error_log('hinasto: ' . $e->getMessage() . ' @ ' . $e->getFile() . ':' . $e->getLine());
    ($isApi ? Response::json(['error' => t('err.server')], 500) : Response::html('<h1>500</h1>', 500))->send();
}
