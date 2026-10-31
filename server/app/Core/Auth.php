<?php
declare(strict_types=1);

namespace Hinasto\Core;

/** Session login for the panel (one admin password) and the CSRF token. */
final class Auth
{
    private const LIFETIME = 30 * 86400;  // a phone app must not ask for the password at every start (sliding: every use moves the expiry)
    private const MAX_AGE = 90 * 86400;   // but no session lives longer than this, however busy

    private static string $cookiePath = '/';

    /** The session cookie is limited to the path the app is served under. */
    public static function setBase(string $base): void
    {
        self::$cookiePath = $base !== '' ? $base : '/';
    }

    public static function start(): void
    {
        if (session_status() === PHP_SESSION_NONE) {
            // own directory: in the shared one other applications clean up sessions with their own (short) lifetime
            $dir = ROOT . '/storage/sessions';
            if ((is_dir($dir) || @mkdir($dir, 0700, true)) && is_writable($dir)) {
                session_save_path($dir);
            }
            ini_set('session.gc_maxlifetime', (string)self::LIFETIME);
            ini_set('session.use_strict_mode', '1');  // an id the server did not issue is never accepted
            session_set_cookie_params(self::cookie(self::LIFETIME));
            session_name('hinasto');
            session_start();
            if (self::valid()) {  // sliding: every use of the panel moves the expiry of a logged in session
                setcookie(session_name(), session_id(), self::cookie(self::LIFETIME, true));
            }
        }
    }

    private static function cookie(int $lifetime, bool $absolute = false): array
    {
        return [($absolute ? 'expires' : 'lifetime') => ($absolute ? time() : 0) + $lifetime,
            'path' => self::$cookiePath, 'secure' => !empty($_SERVER['HTTPS']), 'httponly' => true, 'samesite' => 'Strict'];
    }

    /**
     * A logged in session that is not too old and was made for the current admin password: changing the password
     * (admin_hash) ends every session, whoever holds the cookie.
     */
    private static function valid(): bool
    {
        return !empty($_SESSION['ok'])
            && hash_equals(self::passwordTag(), (string)($_SESSION['h'] ?? ''))
            && time() - (int)($_SESSION['t'] ?? 0) < self::MAX_AGE;
    }

    private static function passwordTag(): string
    {
        return substr(hash('sha256', (string)Config::get('admin_hash', '')), 0, 16);
    }

    public static function requireLogin(): void
    {
        self::start();
        if (!self::valid()) {
            throw new HttpException(401, t('err.login_required'));
        }
    }

    public static function requireCsrf(Request $request): void
    {
        self::start();
        $token = (string)($_SESSION['csrf'] ?? '');
        if ($token === '' || !hash_equals($token, $request->header('X-CSRF'))) {  // never "empty equals empty"
            throw new HttpException(403, t('err.csrf'));
        }
    }

    public static function csrf(): string
    {
        self::start();
        return (string)($_SESSION['csrf'] ?? '');
    }

    /** @return string|null the new CSRF token on success */
    public static function login(string $password): ?string
    {
        self::start();
        $hash = (string)Config::get('admin_hash', '');
        if ($hash === '' || !password_verify($password, $hash)) {
            return null;
        }
        session_regenerate_id(true);
        $_SESSION['ok'] = true;
        $_SESSION['h'] = self::passwordTag();
        $_SESSION['t'] = time();
        $_SESSION['csrf'] = bin2hex(random_bytes(16));
        return $_SESSION['csrf'];
    }

    public static function logout(): void
    {
        self::start();
        $_SESSION = [];
        session_destroy();
        setcookie(session_name(), '', self::cookie(-86400, true));
    }
}
