<?php
declare(strict_types=1);

namespace Hinasto\Controllers;

use Hinasto\Core\Request;
use Hinasto\Core\Response;
use Hinasto\Core\View;

/** The panel page itself: static HTML, all data comes from the JSON API. */
final class PanelController
{
    public function index(Request $request): Response
    {
        return Response::html(View::render('panel', ['title' => t('page.panel'), 'script' => 'app.js']));
    }
}
