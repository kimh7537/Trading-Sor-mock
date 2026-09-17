package com.minisor.channel;

import org.junit.jupiter.api.Test;
import org.springframework.boot.test.context.SpringBootTest;

@SpringBootTest(properties = "minisor.poller.enabled=false")
class ChannelApplicationTests {

	@Test
	void contextLoads() {
	}

}
