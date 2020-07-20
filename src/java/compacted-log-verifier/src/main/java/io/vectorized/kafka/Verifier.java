package io.vectorized.kafka;

import io.vectorized.kafka.configuration.Configuration;
import net.sourceforge.argparse4j.inf.ArgumentParserException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

class Verifier {
  private static final Logger logger = LoggerFactory.getLogger(Verifier.class);

  public static void main(final String[] args) throws Exception {
    final Configuration configuration = new Configuration();

    try {
      Mode mode = configuration.getMode(args);

      switch (mode) {
      case PRODUCER:
        final Producer producer
            = new Producer(configuration.getProducerConfig());
        producer.startProducer();
        break;
      case CONSUMER:
        final Consumer consumer
            = new Consumer(configuration.getConsumerConfig());
        consumer.startConsumer();
        consumer.maybeValidateState();
      }
    } catch (final ArgumentParserException e) {
      configuration.handleError(args, e);
    }
  }
}
