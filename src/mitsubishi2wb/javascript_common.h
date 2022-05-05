
const char count_down_script[] PROGMEM =
"<script>"
    "var count = 10;"
    "(function countDown() {"
        "document.getElementById('count').innerHTML = count + 's';"
        "setTimeout(function() {"
            "if(count > 0) {"
                "count --;"
                "return countDown();"
            "} else {"
                "window.location.href = '/';"
            "}"
        "}, 1000);"
    "})();"
"</script>"
;