<?php
declare(strict_types=1);

namespace Hinasto\Core;

/**
 * Exact-path router. A route is [METHOD, path, [Controller::class, 'method'], guards]; guards are
 * 'auth' (logged in) and 'csrf' (valid X-CSRF header), run in this order before the controller.
 */
final class Router
{
    /** @var array<string, array{0: array, 1: array}> */
    private array $routes = [];
    /** @var array<string, string[]> */
    private array $methodsByPath = [];

    public function __construct(array $routes)
    {
        foreach ($routes as $r) {
            $this->routes[$r[0] . ' ' . $r[1]] = [$r[2], $r[3] ?? []];
            $this->methodsByPath[$r[1]][] = $r[0];
        }
    }

    public function dispatch(Request $request): Response
    {
        $route = $this->routes[$request->method . ' ' . $request->path] ?? null;
        if ($route === null) {
            if (isset($this->methodsByPath[$request->path])) {
                throw new HttpException(405, 'method not allowed');
            }
            throw new HttpException(404, 'not found');
        }
        [$handler, $guards] = $route;
        foreach ($guards as $guard) {
            match ($guard) {
                'auth' => Auth::requireLogin(),
                'csrf' => Auth::requireCsrf($request),
            };
        }
        [$class, $method] = $handler;
        return (new $class())->$method($request);
    }
}
