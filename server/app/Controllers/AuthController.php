<?php
declare(strict_types=1);

namespace Hinasto\Controllers;

use Hinasto\Core\Auth;
use Hinasto\Core\HttpException;
use Hinasto\Core\Request;
use Hinasto\Core\Response;
use Hinasto\Repositories\LoginAttemptRepository;

final class AuthController
{
    private const MAX_FAILS = 8;

    public function login(Request $request): Response
    {
        $attempts = new LoginAttemptRepository();
        $key = LoginAttemptRepository::key($request->ip());
        // The attempt is written BEFORE the password is checked and counted afterwards: requests that arrive together
        // each see the others, so a burst cannot get more tries than the limit allows. Success wipes the count.
        $attempts->addFailure($key);
        if ($attempts->recentFailures($key) > self::MAX_FAILS) {
            throw new HttpException(429, t('err.too_many_attempts'));
        }
        $csrf = Auth::login((string)($request->json(2048)['password'] ?? ''));
        if ($csrf === null) {
            usleep(400000);
            throw new HttpException(401, t('err.bad_password'));
        }
        $attempts->clear($key);
        return Response::json(['ok' => true, 'csrf' => $csrf]);
    }

    public function logout(Request $request): Response
    {
        Auth::logout();
        return Response::json(['ok' => true]);
    }
}
