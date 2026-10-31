<?php
declare(strict_types=1);

namespace Hinasto\Controllers;

use Hinasto\Core\I18n;
use Hinasto\Core\Request;
use Hinasto\Core\Response;
use Hinasto\Core\View;
use Hinasto\Services\ConfigNotWritable;
use Hinasto\Services\Installer;
use RuntimeException;

/** First-run installer. Routed only while storage/config.php does not exist; afterwards /setup is a 404. */
final class SetupController
{
    public function home(Request $request): Response
    {
        return new Response('', 302, ['Location' => $request->base . '/setup']);
    }

    public function form(Request $request): Response
    {
        return $this->page('', [
            'db_host' => 'localhost', 'db_name' => '', 'db_user' => '', 'db_prefix' => 'hn_',
            'timezone' => I18n::defaultTimezone(), 'enroll_key' => '', 'root_user' => '',
        ], 200);
    }

    public function run(Request $request): Response
    {
        $fields = ['db_host', 'db_name', 'db_user', 'db_pass', 'db_prefix', 'root_user', 'root_pass', 'admin_password', 'enroll_key', 'timezone'];
        $in = ['lang' => I18n::lang()];  // already taken from the form by public/index.php
        foreach ($fields as $f) {
            $in[$f] = $request->post($f);
        }
        if ($in['admin_password'] !== $request->post('admin_password2')) {
            return $this->failed($in, t('setup.passwords_differ'));
        }
        try {
            $result = (new Installer())->install($in);
        } catch (ConfigNotWritable $e) {
            return Response::html(View::render('setup_manual', [
                'title' => t('manual.title'),
                'bodyClass' => 'plain',
                'configPhp' => $e->configPhp,
                'enrollKey' => $e->enrollKey,
            ]));
        } catch (RuntimeException $e) {
            return $this->failed($in, $e->getMessage());
        }
        return Response::html(View::render('setup_done', [
            'title' => t('done.title'),
            'bodyClass' => 'plain',
            'enrollKey' => $result['enroll_key'],
        ]));
    }

    private function failed(array $in, string $error): Response
    {
        unset($in['db_pass'], $in['root_pass'], $in['admin_password']);
        return $this->page($error, $in, 422);
    }

    private function page(string $error, array $values, int $status): Response
    {
        return Response::html(View::render('setup', [
            'title' => t('setup.title'),
            'bodyClass' => 'plain',
            'error' => $error,
            'checks' => (new Installer())->preflight(),
            'v' => $values,
            'languages' => I18n::LANGS,
        ]), $status);
    }
}
