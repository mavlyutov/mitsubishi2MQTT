
const char html_init_setup[] PROGMEM = R"====(
<div id='l1' name='l1'></div>
<fieldset>
    <legend><b>&nbsp; _TXT_INIT_TITLE_ &nbsp;</b></legend>
    <form method='post' action='save'>
        <p><b>_TXT_INIT_HOST_</b>
            <br/>
            <input id='hn' name='hn' placeholder=' ' value='_UNIT_NAME_'>
        </p>
        <p><b>_TXT_INIT_SSID_</b> ()
            <br/>
            <input id='ssid' name='ssid' placeholder=' '>
        </p>
        <p><b>_TXT_INIT_PSK_</b> ()
            <br/>
            <input id='psk' name='psk' placeholder=' '>
        </p>
        <p><b>_TXT_INIT_OTA_</b>
            <br/>
            <input id='otapwd' name='otapwd' placeholder=' '>
        </p>
        </p>
        <br/>
        <button name='submit' type='submit' class='button bgrn'> _TXT_SAVE_ </button>
    </form>
</fieldset>
<fieldset>
    <a class="button" href="/reboot" class="back"> _TXT_REBOOT_ </a>
</fieldset>
)====";

const char html_init_save[] PROGMEM =  R"====(
<p> _TXT_INIT_REBOOT_MESS_ </p>
)====";

const char html_init_reboot[] PROGMEM =  R"====(
<p> _TXT_INIT_REBOOT_ </p>
)====";
