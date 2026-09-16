package com.minisor.channel;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.context.properties.ConfigurationPropertiesScan;

/**
 * 채널계.
 *
 * <p>프론트엔드(REST/WebSocket)와 원장(고정 길이 전문 TCP) 사이에 선다.
 *
 * <p><b>이 계층만 Windows에서 빌드한다.</b> C 코드가 WSL을 요구하는 이유는
 * ASan/UBSan 때문인데, Java에는 그 제약이 없다. 설치된 JDK 17과 Node가
 * Windows 쪽에 있으므로 여기서 빌드하는 편이 단순하다.
 */
@SpringBootApplication
@ConfigurationPropertiesScan
public class ChannelApplication {

	public static void main(String[] args) {
		SpringApplication.run(ChannelApplication.class, args);
	}

}
