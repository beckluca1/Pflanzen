function shiftNumbers(parentDiv, count, startIndex, endIndex, time) {
        const firstDiv = parentDiv.querySelector("#number0");
        const lastDiv = parentDiv.querySelector("#number" + (count * 3 + 1));

        const elementHeight = firstDiv.offsetHeight;
        const activeElementHeight = lastDiv.offsetHeight;
        
        const startOffset = 0.5* parentDiv.offsetHeight - 0.5 * activeElementHeight - startIndex * elementHeight;
        const endOffset = 0.5* parentDiv.offsetHeight - 0.5 * activeElementHeight - endIndex * elementHeight;

        for(let i = 0; i <= count * 3; i++) {
          const numberDiv = parentDiv.querySelector("#number" + i);

          numberDiv.animate([
            { transform: 'translate(0px, ' + startOffset + 'px)' },
            { transform: 'translate(0px, ' + endOffset + 'px)' }
          ], {
            duration: time,
            easing: "ease-in-out",
            fill: "forwards"
          });
        }
      }

      function scaleNumbers(parentDiv, count, index, smallFont, bigFont, time) {
        for(let i = 0; i <= count * 3; i++) {
          const numberDiv = parentDiv.querySelector("#number" + i);
          animateNumber(numberDiv, smallFont, smallFont, "0.5", "0.5", 0);
        }

          const endDiv = parentDiv.querySelector("#number" + (count * 3 + 1));
          animateNumber(endDiv, bigFont, bigFont, "1.0", "1.0", 0);

        const activeDiv = parentDiv.querySelector("#number" + index);
        animateNumber(activeDiv, bigFont, bigFont, "1.0", "1.0", 0);
      }

      function scaleText(id, fontSize, opacity) {
        const textDivs = document.getElementsByClassName(id);
        for (let textDiv of textDivs) {
          animateNumber(textDiv, fontSize, fontSize, opacity, opacity, 0);
        }
      }

      function animateNumber(animatedDiv, fonstStart, fontEnd, opacityStart, opacityEnd, time) {
          animatedDiv.animate([
            { fontSize: fonstStart, opacity: opacityStart },
            { fontSize: fontEnd, opacity: opacityEnd }
          ], {
            duration: time,
            easing: "ease-in-out",
            fill: "forwards"
          });
      }

      function transitionNumber(parentDiv, count, startIndex, endIndex, smallFont, bigFont, time) {
        // Get elements
        const startNumberDiv = parentDiv.querySelector("#number" + startIndex);
        const endNumberDiv = parentDiv.querySelector("#number" + endIndex);

        animateNumber(startNumberDiv, bigFont, smallFont, "1.0", "0.5", time);
        animateNumber(endNumberDiv, smallFont, bigFont, "0.5", "1.0", time);
        shiftNumbers(parentDiv, count, startIndex, endIndex, time);
      }

      function scaleNumber(parentDiv, count, oldIndex, startIndex, endIndex, smallFont, bigFont, time) {
        // Set the wapped active number
        if(oldIndex != startIndex) transitionNumber(parentDiv, count, oldIndex, startIndex, smallFont, bigFont, 0);

        // Animate the transition
        if(startIndex != endIndex) transitionNumber(parentDiv, count, startIndex, endIndex, smallFont, bigFont, time);
      }

      async function waitForAnimations(parentDiv, count) {
        for(let i = 0; i <= count * 3; i++) {
          const numberDiv = parentDiv.querySelector("#number" + i);

          const animations = numberDiv.getAnimations();
          const promises = animations.map(anim => anim.finished);
          await Promise.all(promises);
        }
      }

      function wrapIndex(index, count) {
        if(index > count * 2) {
          index -= count;
        }
        else if(index < count) {
          index += count;
        }

        return index;
      }

      async function setActive(parentDiv, count, activeIndex, index, smallFont, bigFont, time) {
        const oldIndex = activeIndex;

        if(index - activeIndex >= 0.5 * count) {
          activeIndex += count;
        }
        else if(index - activeIndex <= -0.5 * count) {
          activeIndex -= count;
        }

        scaleNumber(parentDiv, count, oldIndex, activeIndex, index, smallFont, bigFont, time);

        await waitForAnimations(parentDiv, count);

        return index;
    }

    function setStyle(numberDiv, index, text, fontSize, opacity) {
        numberDiv.textContent = (text.length == 1) ? ("0" + text) : text;
        numberDiv.id = "number" + index;
        numberDiv.style.fontFamily  = 'Verdana, Geneva, sans-serif';
        numberDiv.style.fontSize = fontSize;
        numberDiv.style.opacity = opacity;
        numberDiv.style.color = 'white';
        numberDiv.style.userSelect = 'none';
    }

    function addNumbers(parentDiv, count, increment, smallFont, bigFont) {
      for(let i = 0; i <= count * 3; i++) {
        const numberDiv = document.createElement("div");
        let wrappedIndex = ((i % count) * increment).toString();
        setStyle(numberDiv, i, wrappedIndex, smallFont, "0.5");
        parentDiv.appendChild(numberDiv);
      }

      const endDiv = document.createElement("div");
      setStyle(endDiv, (count * 3 + 1), "end", bigFont, "1.0");
      parentDiv.appendChild(endDiv);
    }

    function getOutput(output, parentDiv, index) {
      const numberDiv = parentDiv.querySelector("#number" + index);
      output.text = numberDiv.textContent;
    }

    async function getInput(id, defaultValue) {
      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 5000);

      try {
        const res = await fetch(id, { signal: controller.signal });

        if (!res.ok) {
          throw new Error(`Server error: ${res.status}`);
        }

        const value = await res.text();
        return parseInt(value.trim());
      } catch (err) {
        if (err.name === "AbortError") {
          console.error("Request timed out (server may be down)");
        } else {
          console.error("Network/server error:", err.message);
        }
      } finally {
        clearTimeout(timeout);
      }

      return defaultValue;
    }

    async function addScrollNumbers(output, id, count, increment=1, defaultValue=0, smallFontH=6, bigFontH=16, smallFontW=6, bigFontW=16) {
      const parentDiv = document.getElementById(id);
      if(parentDiv == null) return;

      let activeIndex = 0;
      let isDragging = false;
      let startX = 0;
      let startY = 0;
      let targetIndex = 0;
      let scrollEnergy = 0;
      let wait = false;

      let smallFont = window.innerHeight < window.innerWidth ? (smallFontH + 'vh') : (smallFontW + 'vw');
      let bigFont = window.innerHeight < window.innerWidth ? (bigFontH + 'vh') : (bigFontW + 'vw');

      addNumbers(parentDiv, count, increment, smallFont, bigFont);
      targetIndex = count + defaultValue / increment;
      activeIndex = await setActive(parentDiv, count, activeIndex, Math.round(targetIndex), smallFont, bigFont, 100);
      getOutput(output, parentDiv, activeIndex);

      parentDiv.addEventListener('wheel', async function(event) {
        scrollEnergy += 0.005 * event.deltaY;
      });
      parentDiv.addEventListener("pointerdown", (e) => {
        isDragging = true;
        startX = e.clientX;
        startY = e.clientY;
        parentDiv.setPointerCapture(e.pointerId);
      });

      const firstDiv = parentDiv.querySelector("#number0");
      const elementHeight = firstDiv.offsetHeight;

      parentDiv.addEventListener("pointermove", async (e) => {
        if (!isDragging) return;
        scrollEnergy -= 1.0 / (elementHeight) * (e.clientY - startY);
        startY = e.clientY;

      });
      parentDiv.addEventListener("pointerup", (e) => {
        isDragging = false;
        parentDiv.releasePointerCapture(e.pointerId);
      });
      parentDiv.addEventListener("pointercancel", (e) => {
        isDragging = false;
      });

      window.addEventListener("resize", async () => {
        smallFont = parentDiv.clientHeight < parentDiv.clientWidth ? (smallFontH + 'vh') : (smallFontW + 'vw');
        bigFont = parentDiv.clientHeight < parentDiv.clientWidth ? (bigFontH + 'vh') : (bigFontW + 'vw');

        scaleNumbers(parentDiv, count, activeIndex, smallFont, bigFont, 0);
        shiftNumbers(parentDiv, count, activeIndex, activeIndex, 0);
      });

      const checkLoop = setInterval(async () => {
        if(wait) return;

        wait = true;

        while(Math.abs(scrollEnergy) > 0.01) {
          targetIndex = wrapIndex(targetIndex + Math.min(scrollEnergy, 1.0), count); 
          const animatonDuration = 200 / (1 + Math.log(1 + Math.abs(scrollEnergy)));
          activeIndex = await setActive(parentDiv, count, activeIndex, Math.round(targetIndex), smallFont, bigFont, animatonDuration);
          getOutput(output, parentDiv, activeIndex);
          scrollEnergy *= 0.5
        }

        wait = false;
      }, 100);

    }

    async function main() {
      let serverUp = true;
      let settingsUp = false;

      let hour = {};
      let minute = {};
      let duration = {};

      let hourDefault = await getInput("/getHour", 12);
      let minuteDefault = await getInput("/getMinute", 0);
      let durationDefault = await getInput("/getDuration", 20);

      await addScrollNumbers(hour, "hours" , 24, 1, hourDefault, 12, 30, 6, 15);
      await addScrollNumbers(minute, "minutes", 12, 5, minuteDefault, 12, 30, 6, 15);
      await addScrollNumbers(duration, "seconds", 50, 5, durationDefault, 6, 12, 3, 6);

      window.addEventListener("resize", async () => {
        const separatorDiv = document.getElementById("separator");
        const bigDiv = document.getElementById("shutdown");

        bigFont = separatorDiv.clientHeight < separatorDiv.clientWidth ? (30 + 'vh') : (15 + 'vw');
        upperFont = 1.5 * bigDiv.clientHeight < bigDiv.clientWidth ? (6 + 'vh') : (3 + 'vw');
        lowerFont = 1.5 * bigDiv.clientHeight < bigDiv.clientWidth ? (4 + 'vh') : (2 + 'vw');

        scaleText("separatorText", bigFont, "1.0");
        scaleText("upperText", upperFont, "1.0");
        scaleText("lowerText", lowerFont, "0.5");
      });

      window.dispatchEvent(new Event("resize"));

      const shutdownDiv = document.getElementById("shutdown");

      shutdownDiv.addEventListener("click", async () => {
        try {
          const response = await fetch("/set?hour=" + hour.text + "&minute=" + minute.text + "&duration=" + duration.text);

          if (!response.ok) {
            throw new Error("HTTP error " + response.status);
          }

          const data = await response.json();
          console.log(data);

          serverUp = false;

          const down = document.getElementById("down");
          down.style.display = "flex";

          const downText = document.getElementById("downText");
          downText.style.display = "block";
        } catch (err) {
          console.error("Fetch failed:", err);
        }
      });

      const waterDiv = document.getElementById("water");

      waterDiv.addEventListener("click", async () => {
        try {
          const response = await fetch("/water?duration=" + duration.text);

          if (!response.ok) {
            throw new Error("HTTP error " + response.status);
          }

          const data = await response.json();
          console.log(data);
        } catch (err) {
          console.error("Fetch failed:", err);
        }
      });

      const settingsText = document.getElementById("settingsText");
      settingsText.addEventListener("click", async () => {
          const settings = document.getElementById("settings");
          settings.style.display = settingsUp ? "none" : "flex";

          settingsUp = !settingsUp;
      });

      const updateDiv = document.getElementById("update");

      updateDiv.addEventListener("click", async () => {
        try {
          const response = await fetch("/update");

          if (!response.ok) {
            throw new Error("HTTP error " + response.status);
          }

          const data = await response.json();
          console.log(data);

          location.reload();
        } catch (err) {
          console.error("Fetch failed:", err);
        }
      });

      const serverUpLoop = setInterval(async () => {
        try {
          const response = await fetch("/health");

          if (!response.ok) {
            throw new Error("HTTP error " + response.status);
          }

          if(serverUp) return;

          serverUp = true;

          const down = document.getElementById("down");
          down.style.display = "none";

          const downText = document.getElementById("downText");
          downText.style.display = "none";
        } catch (err) {
          console.error("Fetch failed:", err);

          if(!serverUp) return;

          serverUp = false;

          const down = document.getElementById("down");
          down.style.display = "flex";

          const downText = document.getElementById("downText");
          downText.style.display = "block";
        }
      }, 10000);
    }

    main();