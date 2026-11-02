package lemuri.fe.v;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import javafx.scene.Scene;
import javafx.scene.layout.StackPane;
import javafx.scene.paint.Color;
import javafx.stage.Stage;
import javafx.stage.StageStyle;
import org.junit.jupiter.api.Test;

/** The F11 fade: in and out, a second F11 halfway, and a hidden window that always comes back fully visible. */
class WindowFadeTest extends UiTest {
    private Stage transparent() {
        return get(() -> {
            Stage s = new Stage(StageStyle.TRANSPARENT);
            s.setScene(new Scene(new StackPane(), 200, 100, Color.TRANSPARENT));
            return s;
        });
    }

    @Test
    void aTransparentWindowFadesItsContentAndComesBackWhole() {
        Stage s = transparent();
        on(() -> {
            WindowFade.hidden(s);
            s.show();
        });
        assertEquals(0.0, get(() -> s.getScene().getRoot().getOpacity()));
        on(() -> WindowFade.in(s));
        until(() -> s.getScene().getRoot().getOpacity() == 1.0, "faded in");
        // The window itself is left alone: on Linux it would ignore it anyway.
        assertEquals(1.0, get(s::getOpacity));

        on(() -> WindowFade.out(s, s::hide));
        assertTrue(get(() -> WindowFade.leaving(s)));
        until(() -> !s.isShowing(), "hidden");
        assertFalse(get(() -> WindowFade.leaving(s)));
        // Shown again some other way (the tray), it is not invisible.
        assertEquals(1.0, get(() -> s.getScene().getRoot().getOpacity()));
    }

    @Test
    void aSecondPressWhileFadingOutBringsItBack() {
        Stage s = transparent();
        on(s::show);
        on(() -> WindowFade.out(s, s::hide));
        on(() -> WindowFade.in(s));
        assertFalse(get(() -> WindowFade.leaving(s)));
        until(() -> s.getScene().getRoot().getOpacity() == 1.0, "back in view");
        assertTrue(get(s::isShowing));
        on(s::close);
    }
}
